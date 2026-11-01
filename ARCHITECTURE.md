# Architecture

The internal design of `boolean-parser`: the byte reader, the lexer state
machine, Dijkstra's shunting-yard to RPN, and the AST build. For usage see
`README.md`; for the repo map and invariants see `AGENTS.md`.

## The pipeline

One class, `BooleanTree`, drives a four-stage pipeline. The first three stages
run in the constructor; the fourth is `Parse()`, called on demand.

```
  input string
      │
      ▼  Lexer: a state machine over the buffer, grouping bytes into Tokens
  token stream
      │
      ▼  toRPN() (shunting-yard): reorder into Reverse Polish notation
  RPN token queue  ──────────────────────────────►  (consumable directly)
      │
      ▼  Parse() → BuildTree(): fold the RPN queue into a node tree
  AST (root)
```

## Stage 1: the input buffer

The constructor copies the input into an owned NUL-terminated buffer (so a
transient `std::string_view` argument is safe): 128 bytes inside the object, a heap
`char[]` for longer input. The lexer holds pointers into it
(`cs`, `pos`) and peeks one symbol ahead; `'\0'` is the end of input. Like
`ContentReader` (which stays in the repo for API compatibility; the lexer does not
use it), it treats a `"\n\r"` or `"\r\n"` pair as one symbol, so the second byte
of a pair never reaches a lexeme. A lexeme is not built byte by byte: it is the run
of bytes from its first symbol up to the current one, copied once with a single
`std::string` construction when the token is returned (plain runs inside a bare
token or a quoted string are skipped in a tight loop).

## Stage 2: the lexer

`Lexer::NextToken()` is a state machine over `LexerState` that consumes symbols
until it has one whole `Token`. Tokens and their lexemes are moved, not copied,
through the shunting-yard and into the `IdNode`. The states:

- `INIT`: skip whitespace; dispatch on the first significant char. `(` `)` `&`
  `|` `!` go to `SYMBOL_OP` (single-char operators); `"` / `'` open a quote; `[`
  opens a list; anything else starts a bare `TOKEN`.
- `TOKEN`: accumulate an identifier until a symbol-op, space, or EOF ends it. A
  quote mid-token switches into `TOKEN_QUOTE`. A token over 1024 chars throws
  `LexicalException`.
- `TOKEN_QUOTE`: copy everything (including spaces) up to the matching quote
  char; `\` enters `ESCAPE` for one char; EOF before the close throws
  `LexicalException`. The quote characters are kept in the lexeme.
- `ESCAPE`: take the next char literally, back to `TOKEN_QUOTE`.
- `INIT_SQUARE_BRACKET` / `END_SQUARE_BRACKET`: the `[a,b,c]` list. Accumulate
  list items (each of which may itself be quoted), accept `,` to continue and
  `]` to finish, throw `LexicalException` on a missing `]`. The whole `[...]`
  becomes one lexeme.
- `SYMBOL_OP`: map the single char to its `TokenType` (`(`→LeftParenthesis,
  `)`→RightParenthesis, `&`→And, `|`→Or, `!`→Not).
- `EOFILE`: emit an `EndOfFile` token.

When a bare `TOKEN` completes, `IsStringOperator()` checks whether the lexeme is
a keyword (`AND`, `OR`, `NOT`, `XOR`, `MAYBE`, case-insensitive) and, if so,
retypes the token from `Id` to the matching operator. So keywords and their
symbolic forms (`AND` vs `&`) converge on the same `TokenType`.

The upshot: an `Id` token's lexeme is the raw source text: a bare term, a
quoted phrase *with its quotes*, or a `[a,b,c]` list *with its brackets and
commas*. Stripping delimiters is the consumer's job.

## Stage 3: shunting-yard to RPN

`toRPN()` is Dijkstra's shunting-yard. It pulls tokens from the lexer and
maintains two structures: `stack_output` (the growing RPN queue, with a head index) and
`stack_operator` (the operator stack). Both are `TokenVector`s (`BooleanTokenVector.h`):
the first 16 (queue) or 8 (stack) tokens live inside the `BooleanTree`, so an ordinary query
allocates nothing here; a longer one moves all its tokens into a `std::vector`, which grows as
usual. A `BooleanTree` is a little over 1 KB for
that reason: build it on the stack, as the examples do.

- An `Id` goes to the output queue. When it follows an operand (an `Id` or `)`), the
  default operator (`DEFAULT_OPERATOR` = `Or`) is first pushed as if written, after
  popping every operator that binds at least as tightly, so `"a b"` becomes `a b OR` and
  `a & b c` becomes `a b & c OR`. A `(` or `NOT` after an operand gets the same implicit
  operator.
- A binary operator pops the operators that bind at least as tightly (left
  associative), then pushes itself; `NOT` is prefix and pops nothing. Binding is
  decided by `precedence()`:

  ```
    Not = 0   (binds tightest)
    And = 1
    Maybe = 2
    Xor = 3
    Or = 4    (binds loosest)
  ```

  The pop condition is `precedence(current) >= precedence(top)`. So `NOT` > `AND` >
  `MAYBE` > `XOR` > `OR`.
- `(` is pushed; `)` pops operators into the output until the matching `(`,
  throwing `SyntacticException` if no `(` is found (unbalanced parens).
- At EOF, any operators left on the stack are flushed to the output.

When the input ends, `validate()` folds the queue with a counter, the way `BuildTree()`
folds it with nodes: an operator needs its operands, a parenthesis left in the queue (an
unclosed `(`) is not an RPN token, and the same messages are thrown. A queue that passes
is empty (no operand at all) or one expression, so the constructor throws for every
malformed input and `Parse()` only fails on a queue that was changed after construction.
Operands cannot outnumber the operators by more than one: every pair of adjacent operands
is joined by an operator, explicit or implicit.

The result in `stack_output` is the expression in RPN, e.g.
`a OR (b AND NOT c)` → `a b c NOT AND OR`. This queue is the primary public
surface: a consumer (Xapiand's query DSL) walks it front-to-back via
`front()`/`pop_front()` and never needs the AST.

## Stage 4: the AST build

`Parse()` calls `BuildTree()`, which folds the RPN queue into a tree **front to
back** with an explicit operand stack:

- An `Id` pushes an `IdNode` (leaf).
- A binary operator (`And`/`Or`/`Xor`/`Maybe`) pops its right operand, then its
  left, and pushes the node.
- `Not` is unary: it pops one operand and pushes a `NotNode`.

So `a b c NOT AND OR` builds `OR(a, AND(b, NOT(c)))`. Running out of operands, or
ending with more than one, is a `SyntacticException`. Nothing recurses, so nesting
is bounded by memory; `destroy_tree()` tears deep trees down the same way, and
trees up to 64 deep use ordinary destruction.
`PrintTree()` post-order-dumps the tree to `std::cout` for debugging.

The node types (`AndNode`, `OrNode`, `NotNode`, `XorNode`, `MaybeNode`,
`IdNode`) are thin: each holds `unique_ptr` children (or, for `IdNode`, the
term string) and reports a `NodeType`. Walking the tree is `getType()` plus the
per-node child getters.

Most of the cost of `Parse()` is one allocation per node, so `BaseNode` has class-specific
`operator new` and `operator delete` (`BooleanParser.cc`) that keep freed blocks of up to
64 bytes in a per-thread list for each 16-byte size class. A block is always allocated at
its class size, so a block freed by one node type serves any other of its class. A thread
keeps at most 256 blocks per class and frees them when it ends, and a block is an ordinary
heap block, so a tree may be destroyed on another thread. Children are plain
`unique_ptr<BaseNode>` with the default deleter.

## Error handling

Two exception types, both plain `std::exception` subclasses owning a
`std::string` message:

- `LexicalException`, from the lexer: unterminated quote, unclosed `[`,
  oversized token, an EOF where a close was expected.
- `SyntacticException`, from the shunting-yard (`)` with no matching `(`), from
  `validate()` (an operator without its operands, an unclosed `(`) and from `Parse()`
  (the same, for a queue changed after construction).

Because lexing, the shunting-yard and `validate()` run in the constructor, a malformed
input throws at construction. `Parse()` can throw later only for a queue that was changed
after construction (or is empty).

Every throw site builds its message in a local `std::string` and passes
`.c_str()`, so both exceptions copy it into a `std::string` they own. The message
stays valid after the stack unwinds.

## What this is not

There is no evaluation, no optimization, and no query backend here. The parser
produces an RPN token queue and (optionally) an AST; turning those into an actual
query is the consumer's job. In Xapiand the query DSL walks the RPN queue to
assemble Xapian queries; that code lives in Xapiand.
