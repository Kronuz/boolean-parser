# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
BooleanParser.{h,cc}           BooleanTree: ctor lexes + shunting-yards to RPN; Parse() builds the AST. The only .cc with public API.
BooleanLexer.{h,cc}            The lexer state machine: tokens, quotes, [a,b,c] lists, symbolic ops.
ContentReader.{h,cc}           Byte-at-a-time reader feeding the lexer (tracks line/column).
BooleanToken.h                 Token + TokenType (Not/Or/And/Maybe/Xor/parens/Id/EndOfFile). Header.
BooleanNode.h                  BaseNode + NodeType. Header.
And/Or/Not/Xor/Maybe/IdNode.h  The AST node types. Headers.
LexicalException.h             Lexer error; owns a std::string. Header.
SyntacticException.h           Parser error; owns a std::string. Header.
test/test.cc                   Runnable smoke test: RPN token stream, precedence, quoting, lists, AST shape, error throws.
CMakeLists.txt                 STATIC library `boolean_parser` (+ alias boolean_parser::boolean_parser); CTest test `boolean_parser`.
LICENSE                        MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md                      What it is, install, usage, API reference.
ARCHITECTURE.md                Internal design: the lexer, the shunting-yard, the AST build.
```

Three `.cc`s are compiled into the library (`BooleanParser.cc`, `BooleanLexer.cc`,
`ContentReader.cc`); everything else is a header. There are **no external
dependencies** — only the C++ standard library.

## Header naming avoids a case-insensitive collision

The sources sit at the repo root, and the library exposes the root as its include
directory, so the public header is reached as `BooleanParser.h`. The three most
generic header names carry a `Boolean` prefix — `BooleanNode.h`, `BooleanToken.h`,
`BooleanLexer.h` — on purpose:

- A root-level `Node.h`/`Token.h`/`Lexer.h` would collide on a case-insensitive
  filesystem (macOS) with a consumer's own lowercase `node.h`/`token.h` on the
  same include path (Xapiand has a `node.h`). The prefix keeps them unambiguous
  without needing a subdirectory.
- The sources include **each other** with bare paths (`#include "BooleanLexer.h"`,
  `#include "BooleanNode.h"`). Those resolve because the files are co-located —
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
  logging/format/MsgPack coupling — keep it that way (see "Standalone vs.
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
  tighter). The shunting-yard pops while `precedence(current) >
  precedence(top)`. The test pins the whole ladder; changing a number changes
  every query's meaning.
- **Two adjacent ids default-join with `OR`** (`DEFAULT_OPERATOR` in
  `BooleanParser.cc`). `"a b"` parses as `a b OR`. The test pins this.
- **Quoted phrases and `[a,b,c]` lists are a single `Id` token** whose lexeme
  keeps its delimiters. The lexer's `TOKEN_QUOTE` / `INIT_SQUARE_BRACKET` /
  `END_SQUARE_BRACKET` states implement this; the test pins both.
- **Both exceptions own their message `std::string`.** `SyntacticException` was
  changed from a bare `const char*` to an owned `std::string` precisely because
  every throw site builds the message from a temporary `std::string` and passes
  `.c_str()`; a borrowed pointer dangled the moment the throw unwound the frame.
  Don't revert it to a borrowed pointer.

## Traps

- **Keep the `Boolean` prefix on the Node/Token/Lexer headers.** See the
  header-naming section above: it's what lets the sources sit at the repo root
  without a macOS case-collision against a consumer's own `node.h`/`token.h`.
  Keep all sources co-located so the bare internal includes resolve.
- **`Parse()` on malformed input can be fragile.** The tree builder's
  dangling-operator path (e.g. `"a AND AND b"`) has a latent upstream defect: it
  can recurse on an emptied output queue and dereference `.back()` on an empty
  list (UB), rather than throwing cleanly. This was inherited from Xapiand and is
  out of scope for the extraction. The test deliberately exercises the *clean*
  error paths (unbalanced parens, unterminated quote, unclosed bracket), which
  throw as expected. If you harden the builder, add the dangling-operator case to
  the test.
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
[Xapiand](https://github.com/Kronuz/Xapiand). The delta from the original is
pure decoupling:

- `BooleanParser.h` dropped `#include "xapian.h"` and the dead
  `Xapian::Query get_query();` method declaration. That method was never defined
  and never called anywhere (Xapiand's `query_dsl.cc` consumes only the RPN token
  queue, plus the two exception types — never the AST or any Xapian type), so its
  removal eliminated 100% of the Xapiand coupling with no behavior change.
- `SyntacticException` was hardened from a borrowed `const char*` to an owned
  `std::string` (matching `LexicalException`), fixing a dangling-pointer bug at
  every throw site. Behavior-neutral for well-formed input; correct for the error
  paths.

The lexer, the shunting-yard, the AST, and the precedence table are otherwise
byte-for-byte the Xapiand originals. Keep extraction hygiene separate from
behavior changes so they can be reconciled with upstream.
