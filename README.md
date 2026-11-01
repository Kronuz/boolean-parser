# boolean-parser

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/boolean-parser/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/boolean-parser/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

Boolean-expression parser for C++17: AND, OR, NOT, XOR and MAYBE queries into a Reverse
Polish token queue or an AST. A shunting-yard turns `a OR (b AND NOT c)` into a queue you
walk in order, and `Parse()` builds typed nodes from it on demand. Reach for it to parse
search-query syntax, filters and rule languages.

## ✨ Highlights

- **Five operators,** as words or symbols: `AND` (`&`), `OR` (`|`), `NOT` (`!`), `XOR` and
  `MAYBE`, with precedence `NOT > AND > MAYBE > XOR > OR` and left associativity.
- **Two outputs.** Walk the RPN queue directly, as Xapiand does, or call `Parse()` for an
  AST of `AndNode`, `OrNode`, `NotNode`, `XorNode`, `MaybeNode` and `IdNode`.
- **Phrases and lists.** `"quoted phrases"` and `[a,b,c]` lists arrive as single terms;
  adjacent terms join with an implicit `OR`.
- **Errors at construction.** Malformed input throws `LexicalException` or
  `SyntacticException` from the constructor, so a queue it returns is empty or one
  well-formed RPN expression.
- **No recursion.** `Parse()` and the tree's teardown are iterative, so nesting depth is
  bounded by memory.
- **Few allocations.** Up to 16 tokens, 8 operators and 128 bytes of input live inside the
  `BooleanTree`, and nodes come from a per-thread cache of freed blocks.
- **Checked against an oracle.** `test/oracle.cc` compares every token sequence up to
  length 7 over a small vocabulary with an independent recursive-descent parser, and a
  libFuzzer target does the same on mutated input.

## 🚀 Quick start

```cpp
#include "BooleanParser.h"

#include <cstdio>

int main() {
	BooleanTree tree("a OR (b AND NOT c)");   // lexes and builds the RPN queue
	for (; !tree.empty(); tree.pop_front()) {
		std::printf("%s ", tree.front().get_lexeme().c_str());
	}
	// a b c NOT AND OR

	BooleanTree ast("a AND b");
	ast.Parse();                               // ast.root: AndNode(IdNode a, IdNode b)
}
```

[`examples/demo.cc`](examples/demo.cc) prints the RPN queue and the AST, drawn as a tree,
for a handful of expressions.

## 📦 Installation

Requires a C++17 compiler and CMake 3.14 or newer, and nothing else. CI builds it on Linux
(x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(boolean_parser
  GIT_REPOSITORY https://github.com/Kronuz/boolean-parser.git
  GIT_TAG        a18c0572abd2cfa219284744890f107e353c6454)  # main tracks the tip
FetchContent_MakeAvailable(boolean_parser)

target_link_libraries(my_app PRIVATE boolean_parser::boolean_parser)
```

Then `#include "BooleanParser.h"`.

## 🎯 When to use it

Use it to parse user-facing boolean query syntax into a structure you evaluate, either by
walking the queue or the tree.

Not for:

- Arithmetic, comparisons or field syntax: it knows boolean operators and terms only.
- Large trees on a hot path: every term and operator is a heap node behind `unique_ptr`,
  so a parser that builds no nodes runs 2 to 4.5 times faster from 16 terms up.
- Recursive walks of untrusted input: `PrintTree()`, and any walk of `root` you write
  recursively, needs its own depth limit.

## ⚡ Performance

Nanoseconds per full parse (lex, shunting-yard and `Parse()`), from `bench/bench_bool.cc`:
the median of three interleaved runs, each the median of about 40 ms of trials, with Apple
clang 17 and `-O3 -DNDEBUG`. The comparator is the benchmark's own precedence-climbing
parser with an arena and `string_view` lexemes; it builds no heap nodes, so it is a floor.

| Input | M4 Pro | M4 Pro, climber | i9-9980HK | i9-9980HK, climber |
|---|---:|---:|---:|---:|
| `alpha AND beta` | 80 ns | 56 ns | 146 ns | 160 ns |
| 5 terms, mixed operators | 196 ns | 119 ns | 395 ns | 273 ns |
| A typical field query | 218 ns | 133 ns | 401 ns | 220 ns |
| 16 random terms | 1.04 µs | 0.49 µs | 2.04 µs | 0.85 µs |
| 128 random terms | 8.35 µs | 4.01 µs | 17.6 µs | 7.09 µs |
| 1,024 random terms | 104 µs | 33.7 µs | 252 µs | 56.6 µs |
| 200 nested parentheses | 2.53 µs | 2.94 µs | 5.29 µs | 4.65 µs |

The machines are an Apple M4 Pro and an Intel Core i9-9980HK.

```sh
cmake -S . -B build -DBOOLEAN_PARSER_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/boolean_parser_bench
```

## 📚 Documentation

- [docs/API.md](docs/API.md): `BooleanTree`, tokens, AST nodes, the grammar, errors and
  caveats.
- [ARCHITECTURE.md](ARCHITECTURE.md): the lexer, the shunting-yard and the tree builder.
- [CHANGELOG.md](CHANGELOG.md): changes by date, with their measurements.
- [AGENTS.md](AGENTS.md): repository map, invariants and traps.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#boolean-parser):
  include line, target and key API.

## 🛠️ Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

`-DBOOLEAN_PARSER_BENCH=ON` builds the benchmark and `-DBOOLEAN_PARSER_FUZZ=ON` the
libFuzzer target (Clang); `build/boolean_parser_demo` runs the tour.

## 🤝 Contributing

Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
before opening a pull request; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC. See
[LICENSE](LICENSE). Derived from [Xapiand](https://github.com/Kronuz/Xapiand)'s query
parser.
