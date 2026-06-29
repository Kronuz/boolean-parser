# boolean-parser

A small C++17 **boolean-expression parser** — lexer, Dijkstra shunting-yard, AST,
and an RPN token stream — extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).

## What it is

`boolean-parser` turns a boolean query string into two consumable forms:

1. An **RPN token queue** — the input lexed and reordered into Reverse Polish
   notation by Dijkstra's shunting-yard algorithm. This is the queue Xapiand's
   query DSL walks to build its own query objects.
2. An **AST** — a tree of typed nodes (`AndNode`, `OrNode`, `NotNode`,
   `XorNode`, `MaybeNode`, `IdNode`), built on demand from the RPN queue.

It understands the operators `AND`, `OR`, `NOT`, `XOR`, and `MAYBE` (both as
keywords and the symbolic `&` `|` `!`), parentheses for grouping, single- and
double-quoted phrases, and a `[a,b,c]` bracketed-list syntax. Operator
precedence is `NOT` > `AND` > `MAYBE` > `XOR` > `OR`.

Everything is one class, `BooleanTree`:

```
  "a OR (b AND NOT c)"
        │
        ▼   BooleanTree(input)   — lex + shunting-yard, in the constructor
  RPN queue:  a  b  c  NOT  AND  OR        ← walk this directly (front/pop_front)
        │
        ▼   Parse()              — consume the RPN queue
  AST:        OR( a, AND( b, NOT(c) ) )    ← walk this (root + node getters)
```

## How it works

The constructor `BooleanTree(std::string_view)` does all the lexing and parsing
up front. A `Lexer` (driven by a byte-at-a-time `ContentReader`) produces a
stream of `Token`s; `toRPN()` runs Dijkstra's shunting-yard over them, pushing
operands to an output queue and shuffling operators through an operator stack by
precedence, leaving an RPN token queue in `stack_output`.

Two adjacent terms with no operator between them are joined by a default
operator (`OR`), so `"a b"` parses as `a b OR`.

`Parse()` then consumes that RPN queue back-to-front to build the AST into
`root`, turning each operator token into the matching node and each id into an
`IdNode`. You can use either form — many consumers (Xapiand included) only ever
walk the RPN queue and never call `Parse()`.

Malformed input throws: the lexer throws `LexicalException` (unterminated quote,
unclosed `[`, oversized token), and the shunting-yard / tree builder throw
`SyntacticException` (unbalanced parentheses, an unexpected leftover token).
Both are plain `std::exception` subclasses that own their message string.

## Install

CMake with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  boolean_parser
  GIT_REPOSITORY https://github.com/Kronuz/boolean-parser.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(boolean_parser)

target_link_libraries(your_target PRIVATE boolean_parser::boolean_parser)
```

The `boolean_parser` target is a small `STATIC` library (it compiles three
`.cc`s — `BooleanParser.cc`, `BooleanLexer.cc`, `ContentReader.cc`), requests
`cxx_std_17`, and puts the repo root on your include path. The public header is:

```cpp
#include "BooleanParser.h"   // BooleanTree, Token, TokenType
```

The three most generic internal headers carry a `Boolean` prefix —
`BooleanNode.h`, `BooleanToken.h`, `BooleanLexer.h` — so that, sitting at the
include-path root, they don't collide on a case-insensitive filesystem (macOS)
with a consumer's own lowercase `node.h`/`token.h`. The bare internal includes
(`#include "BooleanLexer.h"`) resolve among the co-located sources.

Requires C++17. On macOS it builds with AppleClang/libc++, the same toolchain
Xapiand uses.

## Usage

```cpp
#include "BooleanParser.h"
#include <cstdio>

// Lex + build the RPN queue (this happens in the constructor):
BooleanTree tree("a OR (b AND NOT c)");

// Option 1 — walk the RPN token queue directly (front-to-back):
while (!tree.empty()) {
    const Token& t = tree.front();
    if (t.get_type() == TokenType::Id) {
        std::printf("term: %s\n", t.get_lexeme().c_str());
    } else {
        std::printf("op\n");               // And / Or / Not / Xor / Maybe
    }
    tree.pop_front();
}

// Option 2 — build the AST and walk the nodes:
BooleanTree tree2("a AND b");
tree2.Parse();
// tree2.root is an AndNode over two IdNodes; getType() / getLeftNode() /
// getRightNode() / IdNode::getId() let you walk it.
```

Quoted phrases and bracket lists come through as a single `Id` token whose
lexeme keeps its delimiters: `"hello world"` → one id with lexeme
`"hello world"`; `[x,y,z]` → one id with lexeme `[x,y,z]`.

## API reference

### `BooleanTree`

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

### `Token` / `TokenType`

- `TokenType get_type()` — one of `Not`, `Or`, `And`, `Maybe`, `Xor`,
  `LeftParenthesis`, `RightParenthesis`, `Id`, `EndOfFile`.
- `const std::string& get_lexeme()` — the source text (for an `Id`, the term;
  quotes and brackets preserved).

### AST nodes (`BooleanNode.h` + the `*BooleanNode.h` headers)

- `BaseNode::getType() -> NodeType` (`AND`, `OR`, `NOT`, `XOR`, `MAYBE`, `ID`).
- `AndNode` / `OrNode` / `XorNode` / `MaybeNode`: `getLeftNode()`,
  `getRightNode()`.
- `NotNode`: `getNode()`.
- `IdNode`: `getId() -> std::string`.

### Exceptions

- `LexicalException` — thrown by the lexer. Owns a `std::string`; `what()` is the
  message.
- `SyntacticException` — thrown by the shunting-yard / tree builder. Owns a
  `std::string`; `what()` is the message.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The test parses a spread of expressions and asserts on the RPN token stream
(operators, parentheses, the symbolic spellings, precedence across the full
`NOT > AND > MAYBE > XOR > OR` ladder, adjacent-id default-join, quoted phrases,
and the `[a,b,c]` list), checks `Parse()` yields the right AST shape, and
confirms malformed inputs throw `LexicalException` / `SyntacticException`. It
prints `all boolean-parser tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/boolean_parser_demo
```

For each of a handful of expressions it prints all three forms: the raw input,
the RPN token stream the constructor produced, and the AST (from `Parse()`) drawn
as an indented tree. So you can watch one source string resolve into both
consumable shapes, and watch precedence and grouping fall out of the
shunting-yard. It runs an explicitly-parenthesized expression, two that lean on
precedence alone (`NOT > AND > MAYBE > XOR > OR`), the symbolic spellings
(`& | !`) and the adjacent-id default-join with `OR`, a quoted phrase and a
`[a,b,c]` list each as a single `Id`, and a malformed input that throws a
`SyntacticException` instead of producing a tree.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand), where this parser
feeds the query DSL: Xapiand walks the RPN token queue to build its Xapian
queries. The standalone delta is pure decoupling — the parser had exactly one
tie to Xapiand, a dead `Xapian::Query get_query()` declaration (never defined,
never called) and its `#include "xapian.h"`, both removed. On the way out,
`SyntacticException` was hardened to own its message string (it stored a bare
`const char*` that dangled when constructed from a temporary). The parser logic
is otherwise identical. See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and
[AGENTS.md](AGENTS.md) for the repo map and invariants.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
