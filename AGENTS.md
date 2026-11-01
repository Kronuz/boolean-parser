# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
BooleanParser.{h,cc}           BooleanTree: ctor lexes + shunting-yards to RPN; Parse() builds the AST. The only .cc with public API.
BooleanLexer.{h,cc}            The lexer state machine: tokens, quotes, [a,b,c] lists, symbolic ops.
ContentReader.{h,cc}           Byte-at-a-time reader (tracks line/column). Kept for API compatibility; the lexer does not use it (it reads the buffer by pointer and applies the same "\n\r" pair rule).
test/diff.cc                   Differential test (CTest `boolean_parser_diff`) against test/frozen, the original sources compiled with `Old`-prefixed names; the two differ only in the precedence classes of test/oracle.h `changed_class`.
test/oracle.{h,cc}             Reference recursive-descent parser and the exhaustive/random oracle test (CTest `boolean_parser_oracle`; BP_MAX_LEN=9 for the deepest run, default 7).
test/fuzz.cc, test/fuzz.dict   libFuzzer target with a grammar-aware mutator: queue well-formedness, accept/reject and tree against the oracle, PrintTree (cmake -DBOOLEAN_PARSER_FUZZ=ON, clang).
bench/bench_bool.cc            Benchmark vs the frozen original and an arena precedence-climbing parser (cmake -DBOOLEAN_PARSER_BENCH=ON).
BooleanToken.h                 Token + TokenType (Not/Or/And/Maybe/Xor/parens/Id/EndOfFile). Header.
BooleanTokenVector.h           TokenVector<N>: the RPN queue and the operator stack, the first N tokens inside the object. Header.
BooleanNode.h                  BaseNode + NodeType. Header.
And/Or/Not/Xor/Maybe/IdNode.h  The AST node types. Headers.
LexicalException.h             Lexer error; owns a std::string. Header.
SyntacticException.h           Parser error; owns a std::string. Header.
test/test.cc                   Runnable smoke test: RPN token stream, precedence, quoting, lists, AST shape, error throws.
CMakeLists.txt                 STATIC library `boolean_parser` (+ alias boolean_parser::boolean_parser); CTest tests `boolean_parser`, `_diff`, `_oracle`.
LICENSE                        MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC.
CHANGELOG.md                   Behavior and performance changes, dated.
README.md                      What it is, install, quick start.
docs/API.md                    The API reference and caveats.
docs/boolean-parser.d2         The README diagram's source (docs/boolean-parser.svg is rendered from it).
ARCHITECTURE.md                Internal design: the lexer, the shunting-yard, the AST build.
```

Three `.cc`s are compiled into the library (`BooleanParser.cc`, `BooleanLexer.cc`,
`ContentReader.cc`); everything else is a header. There are **no external
dependencies**, only the C++ standard library.

## Header naming avoids a case-insensitive collision

The sources sit at the repo root, and the library exposes the root as its include
directory, so the public header is reached as `BooleanParser.h`. The three most
generic header names carry a `Boolean` prefix on purpose (`BooleanNode.h`,
`BooleanToken.h`, `BooleanLexer.h`):

- A root-level `Node.h`/`Token.h`/`Lexer.h` would collide on a case-insensitive
  filesystem (macOS) with a consumer's own lowercase `node.h`/`token.h` on the
  same include path (Xapiand has a `node.h`). The prefix keeps them unambiguous
  without needing a subdirectory.
- The sources include **each other** with bare paths (`#include "BooleanLexer.h"`,
  `#include "BooleanNode.h"`). Those resolve because the files are co-located:
  the compiler finds a sibling header relative to the including file. Keep them
  together; don't split headers into a separate `include/` tree or the bare
  includes break.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Expected output ends with `all boolean-parser tests passed`, exit 0. The CMake
`boolean_parser` target is a `STATIC` library that requests `cxx_std_17` and
exposes the repo root as a `PUBLIC` include. The test target is
`boolean_parser_test`; the registered CTest name is `boolean_parser`.

## Conventions

- **C++17.** The code uses `std::string_view` and `[[fallthrough]]` and nothing
  newer, so the target requests `cxx_std_17` for portability. Don't reach for
  C++20 features without bumping the standard (and the reason to).
- **No external dependencies.** The only includes are the C++ standard library
  and the parser's own co-located headers. There are no Xapiand headers, no
  logging/format/MsgPack coupling. Keep it that way (see "Standalone vs.
  Xapiand").
- **Errors are the parser's own two exception types.** `LexicalException` from
  the lexer, `SyntacticException` from the shunting-yard / tree builder. Both are
  plain `std::exception` subclasses that own a `std::string`. Don't introduce a
  `THROW` macro or a third error channel.
- Tabs for indentation, double quotes in code, no em dashes in prose.

## Load-bearing invariants

- **The RPN token queue is the primary public surface.** Xapiand consumes
  `BooleanTree` by walking `stack_output` through `empty()` / `front()` /
  `pop_front()` (and the rest), and never calls `Parse()` or touches the AST. Do
  not change the queue's element type, ordering, or the deque-like method names
  without understanding that the main consumer depends on exactly this.
- **Operator precedence is `NOT > AND > MAYBE > XOR > OR`.** Encoded in
  `BooleanTree::precedence` as `Not=0, And=1, Maybe=2, Xor=3, Or=4` (lower binds
  tighter). The shunting-yard pops while `precedence(current) >=
  precedence(top)` (left associative; `NOT` pops nothing, being prefix). The test pins the whole ladder; changing a number changes
  every query's meaning.
- **Two adjacent ids default-join with `OR`** (`DEFAULT_OPERATOR` in
  `BooleanParser.cc`). `"a b"` parses as `a b OR`, and the implicit `OR` obeys precedence like an explicit one. The oracle test pins this.
- **Quoted phrases and `[a,b,c]` lists are a single `Id` token** whose lexeme
  keeps its delimiters. The lexer's `TOKEN_QUOTE` / `INIT_SQUARE_BRACKET` /
  `END_SQUARE_BRACKET` states implement this; the test pins both.
- **The constructor returns an empty queue or one well-formed RPN expression.**
  `validate()` runs after `toRPN()` and throws what `BuildTree()` throws for the same
  token, so a consumer that walks the queue and never calls `Parse()` meets no operator
  without operands and no parenthesis. `test/test.cc` pins the messages, `test/oracle.cc`
  checks every token sequence (a returned queue always parses), and `test/diff.cc` lists
  these rejections as their own class. Keep the check in sync with `BuildTree()`.
- **Both exceptions own their message `std::string`.** Every throw site builds
  the message in a temporary `std::string` and passes `.c_str()`, so a borrowed
  `const char*` dangles the moment the throw unwinds the frame. Don't make either
  exception borrow its message.

## Implicit OR and associativity

`toRPN` emits the implicit `OR` before
the term that follows it, after popping the operators that bind at least as tightly,
so `a & b c` is `(a AND b) OR c` and `NOT a b` is `(NOT a) OR b`. Binary operators are
left associative. A term, `(` or `NOT` right after a term or `)` takes the implicit `OR`.
`test/oracle.cc` pins all of it against the reference parser in `test/oracle.h`, and
`test/diff.cc` lists the input classes that differ from the frozen original
(`oracle.h`, `changed_class`). `CHANGELOG.md` has the old behavior.

## Traps

- **Keep the `Boolean` prefix on the Node/Token/Lexer headers.** See the
  header-naming section above: it's what lets the sources sit at the repo root
  without a macOS case-collision against a consumer's own `node.h`/`token.h`.
  Keep all sources co-located so the bare internal includes resolve.
- **`Parse()` is iterative.** It folds the RPN queue with an explicit operand stack
  (32 inline, then heap), popping the right operand before the left, and
  `BooleanTree` tears deep trees down with `destroy_tree()`. Keep both: recursion
  overflowed a 512 KiB stack near 1,000 levels, and building a node from two
  `BuildTree()` calls in one argument list swapped MAYBE's operands under clang
  (argument evaluation order is unspecified). Trees up to 64 deep are destroyed by
  ordinary recursion, which keeps the common case allocation-free.
- **Nodes come from a per-thread block cache.** `BaseNode::operator new` and `operator delete`
  (`BooleanParser.cc`) keep freed blocks of up to 64 bytes per 16-byte class, up to 256 per
  class per thread, and free them at thread exit. Both use one `node_class()`, and a block is
  allocated at its class size: a smaller block in a larger class's list overflows when it is
  reused. ASan and MSan builds skip the cache (`BOOLEAN_PARSER_NO_NODE_CACHE`) so they still
  see a use after free. `test/test.cc` builds trees across threads and holds many alive.
- **`get_lexeme()` returns the raw source text.** For a quoted phrase the lexeme
  still has the quotes; for a list it still has the brackets and commas. Consumers
  that want the inner value strip the delimiters themselves. Don't silently change
  what the lexeme contains.
- **The constructor does the work.** Lexing and the shunting-yard run in
  `BooleanTree`'s constructor, so a malformed input throws at construction, not at
  some later call. Tests and consumers must wrap construction in try/catch, not
  just `Parse()`.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). It differs from upstream in these
ways, each listed with its date in `CHANGELOG.md`:

- `BooleanParser.h` has no `#include "xapian.h"` and no `get_query()`
  declaration. Xapiand's `query_dsl.cc` uses only the RPN token queue and the two
  exception types, never the AST or any Xapian type.
- `SyntacticException` owns its message.
- The lexer scans by pointer, tokens move, and the RPN queue and operator stack keep their first tokens inside the object.
- The implicit `OR` obeys precedence and binary operators are left associative.

`test/frozen` keeps the Xapiand originals for the differential test. Keep
extraction hygiene separate from behavior changes so they can be reconciled with
upstream.
