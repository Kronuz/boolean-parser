# boolean-parser API reference

## `BooleanTree`

The whole public surface.

- `explicit BooleanTree(std::string_view input)` — lexes `input` and builds the
  RPN token queue, in the constructor. May throw `LexicalException` /
  `SyntacticException` for malformed input.
- `void Parse()` — consume the RPN queue to build the AST into `root`. May throw
  `SyntacticException`. `root` is a `std::unique_ptr<BaseNode>`.
- `void PrintTree()` — debug-dump the AST (post-order) to `std::cout`.

The RPN queue (a `std::list<Token>`) is exposed as a deque-like surface:

- `bool empty()`, `size_t size()`
- `Token& front()` / `back()` (and const overloads)
- `void pop_front()`, `void pop_back()`

## `Token` / `TokenType`

- `TokenType get_type()` — one of `Not`, `Or`, `And`, `Maybe`, `Xor`,
  `LeftParenthesis`, `RightParenthesis`, `Id`, `EndOfFile`.
- `const std::string& get_lexeme()` — the source text (for an `Id`, the term;
  quotes and brackets preserved).

## AST nodes (`BooleanNode.h` + the `*BooleanNode.h` headers)

- `BaseNode::getType() -> NodeType` (`AND`, `OR`, `NOT`, `XOR`, `MAYBE`, `ID`).
- `AndNode` / `OrNode` / `XorNode` / `MaybeNode`: `getLeftNode()`,
  `getRightNode()`.
- `NotNode`: `getNode()`.
- `IdNode`: `getId() -> std::string`.

## Exceptions

- `LexicalException` — thrown by the lexer. Owns a `std::string`; `what()` is the
  message.
- `SyntacticException` — thrown by the shunting-yard / tree builder. Owns a
  `std::string`; `what()` is the message.

## Caveats

- `Parse()` and the tree's teardown never recurse, so nesting depth is bounded by
  memory, not the stack. Code that walks `root` recursively (like `PrintTree()`)
  still needs its own limit for untrusted input.
- Quoted phrases and bracket lists arrive as one `Id` whose lexeme keeps its
  quotes or brackets.
