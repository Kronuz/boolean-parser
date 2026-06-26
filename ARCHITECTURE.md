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
      ▼  ContentReader        — hand out one Symbol (char + line/column) at a time
  symbol stream
      │
      ▼  Lexer                — a state machine grouping symbols into Tokens
  token stream
      │
      ▼  toRPN() (shunting-yard) — reorder into Reverse Polish notation
  RPN token queue  ──────────────────────────────►  (consumable directly)
      │
      ▼  Parse() → BuildTree()   — fold the RPN queue into a node tree
  AST (root)
```

## Stage 1: `ContentReader`

The constructor copies the input into an owned `char[]` buffer (so a transient
`std::string_view` argument is safe) and wraps it in a `ContentReader`.
`NextSymbol()` returns a `Symbol` — the next `char` plus its line and column —
advancing one byte per call and returning `'\0'` at end of input. It is the
lexer's only input. Line/column are tracked for diagnostics; the parser does not
otherwise use them.

## Stage 2: the lexer

`Lexer::NextToken()` is a state machine over `LexerState` that consumes symbols
until it has one whole `Token`. The states:

- `INIT` — skip whitespace; dispatch on the first significant char. `(` `)` `&`
  `|` `!` go to `SYMBOL_OP` (single-char operators); `"` / `'` open a quote; `[`
  opens a list; anything else starts a bare `TOKEN`.
- `TOKEN` — accumulate an identifier until a symbol-op, space, or EOF ends it. A
  quote mid-token switches into `TOKEN_QUOTE`. A token over 1024 chars throws
  `LexicalException`.
- `TOKEN_QUOTE` — copy everything (including spaces) up to the matching quote
  char; `\` enters `ESCAPE` for one char; EOF before the close throws
  `LexicalException`. The quote characters are kept in the lexeme.
- `ESCAPE` — take the next char literally, back to `TOKEN_QUOTE`.
- `INIT_SQUARE_BRACKET` / `END_SQUARE_BRACKET` — the `[a,b,c]` list: accumulate
  list items (each of which may itself be quoted), accept `,` to continue and
  `]` to finish, throw `LexicalException` on a missing `]`. The whole `[...]`
  becomes one lexeme.
- `SYMBOL_OP` — map the single char to its `TokenType` (`(`→LeftParenthesis,
  `)`→RightParenthesis, `&`→And, `|`→Or, `!`→Not).
- `EOFILE` — emit an `EndOfFile` token.

When a bare `TOKEN` completes, `IsStringOperator()` checks whether the lexeme is
a keyword (`AND`, `OR`, `NOT`, `XOR`, `MAYBE`, case-insensitive) and, if so,
retypes the token from `Id` to the matching operator. So keywords and their
symbolic forms (`AND` vs `&`) converge on the same `TokenType`.

The upshot: an `Id` token's lexeme is the raw source text — a bare term, a
quoted phrase *with its quotes*, or a `[a,b,c]` list *with its brackets and
commas*. Stripping delimiters is the consumer's job.

## Stage 3: shunting-yard to RPN

`toRPN()` is Dijkstra's shunting-yard. It pulls tokens from the lexer and
maintains two structures: `stack_output` (a `std::list<Token>`, the growing RPN
queue) and `stack_operator` (a `std::vector<Token>`, the operator stack).

- An `Id` is appended straight to the output queue. If the previous token was
  also an `Id` (two adjacent terms with no operator), a default operator
  (`DEFAULT_OPERATOR` = `Or`) is injected between them, respecting precedence —
  so `"a b"` becomes `a b OR`.
- An operator pops higher-or-equal-binding operators off the stack into the
  output, then pushes itself. Binding is decided by `precedence()`:

  ```
    Not = 0   (binds tightest)
    And = 1
    Maybe = 2
    Xor = 3
    Or = 4    (binds loosest)
  ```

  The pop condition is `precedence(current) > precedence(top)` — i.e. pop while
  the stack top binds at least as tightly. So `NOT` > `AND` > `MAYBE` > `XOR` >
  `OR`.
- `(` is pushed; `)` pops operators into the output until the matching `(`,
  throwing `SyntacticException` if no `(` is found (unbalanced parens).
- At EOF, any operators left on the stack are flushed to the output.

The result in `stack_output` is the expression in RPN, e.g.
`a OR (b AND NOT c)` → `a b c NOT AND OR`. This queue is the primary public
surface: a consumer (Xapiand's query DSL) walks it front-to-back via
`front()`/`pop_front()` and never needs the AST.

## Stage 4: the AST build

`Parse()` calls `BuildTree()`, which folds the RPN queue into a tree by reading
it **back-to-front** (`stack_output.back()` / `pop_back()`):

- An `Id` becomes an `IdNode` (leaf).
- A binary operator (`And`/`Or`/`Xor`/`Maybe`) pops itself and recurses twice
  for its two children — `OrNode(BuildTree(), BuildTree())`, and so on.
- `Not` is unary: `NotNode(BuildTree())`.

So `a b c NOT AND OR` rebuilds as `OR(a, AND(b, NOT(c)))`. After the build,
`Parse()` checks the queue is empty; a leftover token is a `SyntacticException`.
`PrintTree()` post-order-dumps the tree to `std::cout` for debugging.

The node types (`AndNode`, `OrNode`, `NotNode`, `XorNode`, `MaybeNode`,
`IdNode`) are thin: each holds `unique_ptr` children (or, for `IdNode`, the
term string) and reports a `NodeType`. Walking the tree is `getType()` plus the
per-node child getters.

## Error handling

Two exception types, both plain `std::exception` subclasses owning a
`std::string` message:

- `LexicalException` — from the lexer: unterminated quote, unclosed `[`,
  oversized token, an EOF where a close was expected.
- `SyntacticException` — from the shunting-yard (`)` with no matching `(`) and
  from `Parse()` (an unexpected leftover token).

Because lexing and the shunting-yard run in the constructor, a malformed input
throws at construction. `Parse()` can throw later for a structurally invalid RPN
queue.

`SyntacticException` originally borrowed a `const char*`, which dangled because
every throw site builds its message in a local `std::string` and passes
`.c_str()`. It now owns a `std::string` (matching `LexicalException`), so the
message stays valid after the stack unwinds — the one behavior fix made during
extraction.

## What this is not

There is no evaluation, no optimization, and no query backend here. The parser
produces an RPN token queue and (optionally) an AST; turning those into an actual
query is the consumer's job. In Xapiand the query DSL walks the RPN queue to
assemble Xapian queries — that lived outside this code and stayed in Xapiand.
