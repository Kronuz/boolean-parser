# boolean-parser API reference

## `BooleanTree`

The whole public surface.

- `explicit BooleanTree(std::string_view input)`: lexes `input` and builds the
  RPN token queue, in the constructor. Throws `LexicalException` /
  `SyntacticException` for malformed input. A queue the constructor returns is
  empty (no operand at all) or one well-formed RPN expression, so a consumer that
  walks it never meets an operator without its operands or a parenthesis.
- `void Parse()`: consume the RPN queue to build the AST into `root`. Throws
  `SyntacticException` when the queue is empty or was changed by `pop_front()` /
  `pop_back()` into something that is not an expression. `root` is a
  `std::unique_ptr<BaseNode>`.
- `void PrintTree()`: debug-dump the AST (post-order) to `std::cout`.

The RPN queue (a vector with a moving head) is exposed as a deque-like surface:

- `bool empty()`, `size_t size()`
- `Token& front()` / `back()` (and const overloads)
- `void pop_front()`, `void pop_back()`

## `Token` / `TokenType`

- `TokenType get_type()`: one of `Not`, `Or`, `And`, `Maybe`, `Xor`,
  `LeftParenthesis`, `RightParenthesis`, `Id`, `EndOfFile`.
- `const std::string& get_lexeme()`: the source text (for an `Id`, the term;
  quotes and brackets preserved).

## AST nodes (`BooleanNode.h` + the `*BooleanNode.h` headers)

- `BaseNode::getType() -> NodeType` (`AND`, `OR`, `NOT`, `XOR`, `MAYBE`, `ID`).
- `AndNode` / `OrNode` / `XorNode` / `MaybeNode`: `getLeftNode()`,
  `getRightNode()`.
- `NotNode`: `getNode()`.
- `IdNode`: `getId() -> std::string`.

## Exceptions

- `LexicalException`: thrown by the lexer. Owns a `std::string`; `what()` is the
  message.
- `SyntacticException`: thrown by the shunting-yard / tree builder. Owns a
  `std::string`; `what()` is the message.

## Caveats

- `Parse()` and the tree's teardown never recurse, so nesting depth is bounded by
  memory, not the stack. Code that walks `root` recursively (like `PrintTree()`)
  still needs its own limit for untrusted input.
- Quoted phrases and bracket lists arrive as one `Id` whose lexeme keeps its
  quotes or brackets.
- Nodes are allocated from a small per-thread cache of freed blocks (`BaseNode`'s
  `operator new` and `operator delete`, in `BooleanParser.cc`), so a tree can be built
  on one thread and destroyed on another. Blocks go back to the system allocator
  beyond 256 per size class and when a thread ends. AddressSanitizer and
  MemorySanitizer builds use the system allocator, or define
  `BOOLEAN_PARSER_NO_NODE_CACHE` to do the same.
