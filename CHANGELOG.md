# Changelog

All notable changes to this library. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library has no
release tags yet, so entries are dated.

## 2026-10-31

### Documentation

- The README records the machine for the parser benchmark: Apple M4 Pro, Apple clang
  17, `-O3`, at `adf5b11`. The same run times the frozen original (`test/frozen`)
  that the lexer and queue rewrite below replaced:

  | workload | frozen original | `BooleanTree` |
  |---|---:|---:|
  | 2 terms | 263 ns | 164 ns |
  | 5 terms mixed | 592 ns | 320 ns |
  | typical query | 780 ns | 344 ns |
  | 16 random terms | 2.95 us | 1.40 us |
  | 128 random terms | 25.3 us | 11.1 us |
  | 1024 random terms | 186 us | 107 us |
  | 200 nested parentheses | 5.09 us | 2.59 us |

### Fixed

- **Implicit `OR` precedence and associativity in `toRPN`.** The shunting-yard
  emitted a term before popping the operators that bind tighter than the implicit
  `OR`, so `a & b c` was `a OR (b AND c)`, `x AND a b` was `x OR (a AND b)` and
  `NOT a b` was `a OR (NOT b)`. It also popped only on strictly tighter
  operators, so equal-precedence chains grouped to the right (`a | b | c` was
  `a | (b | c)`): the same truth table for `AND`, `OR` and `XOR`, a different RPN
  queue and tree, and a different meaning for `MAYBE`.
  - The implicit `OR` is a binary operator: it pops everything that binds at
    least as tightly, is pushed, and then the term follows. Binary operators are
    left associative; `NOT` is prefix and pops nothing.
  - A term, `(` or `NOT` after a term or `)` takes the implicit `OR`. `a NOT b`
    and `a ( NOT b )` were rejected, and `a ( b ) & c` was `(a OR b) AND c`.
  - Malformed input the old queue accepted by dropping operands (`a NOT` as
    `NOT a`, `a ()` as `a`, `( ) a`, `a ( NOT )`) is rejected.
  - Messages for rejected input may name a different token.
  - Inputs with no implicit operator next to another operator and no repeated
    same-level operator parse exactly as before.

  Oracle: `test/oracle.h`, an independent recursive-descent parser written from
  the documented grammar. `test/oracle.cc` compares accept or reject, RPN and tree
  on every token sequence up to length 9 over a 7-token vocabulary (47M
  sequences, through `BP_MAX_LEN`), every sequence up to length 5 over a 12-token
  vocabulary with operator spellings, and 300k random longer ones.

- **`PrintTree()` reads a `MAYBE` node as a `MaybeNode`.** The `MAYBE` case cast the
  node to `AndNode` (copied from the `AND` case), so printing any tree with a `MAYBE`
  node was undefined behavior (a segfault in practice). `test/test.cc` compares the
  output with an independent in-order renderer and pins the layout of `a MAYBE b`.
- **Malformed input throws at construction, as the README says.** An unclosed `(`
  (`(a`, `a (`, `a AND (b`) and an operator without its operands (`a NOT`, `a AND`,
  `AND a`, `a ()`) left the RPN queue holding a token that is not RPN, and the error
  came from `Parse()` or never. The constructor now runs `validate()` after the
  shunting-yard, which folds the queue with an operand counter and throws the
  `SyntacticException` that `BuildTree()` threw for the same queue: `'(' not expected`
  for a stray `(` and `Unexpected end of expression` for a missing operand. A queue the
  constructor returns is empty (no operand at all: blank input, `()`) or one well-formed
  RPN expression.
  - Behavior change for callers that read the queue and never call `Parse()` (Xapiand's
    `query_dsl.cc` reads `stack_output`): input that used to construct and carry a
    stray `(` in the queue now throws `SyntacticException` from the constructor. The
    exception type and messages are those that `Parse()` gave for the same input; the
    stage that throws moves up.
  - `test/test.cc` has a table of malformed inputs with their exact messages;
    `test/oracle.cc` requires that every queue it constructs parses; `test/diff.cc`
    compares the constructor and `Parse()` with the original on malformed input;
    `test/fuzz.cc` is a libFuzzer target (`-DBOOLEAN_PARSER_FUZZ=ON`, with
    `test/fuzz.dict`) with a grammar-aware mutator, an independent stack machine and
    the oracle: 950,000 runs under UBSan and 157,000 under ASan with UBSan found nothing.

### Changed

- **Small trees do not allocate for their containers, and nodes come from a
  per-thread cache.** The node API (`unique_ptr<BaseNode>` children, `release_children`,
  every public type) is unchanged.
  - `stack_output` and `stack_operator` are `TokenVector`s (`BooleanTokenVector.h`),
    which keep the first 16 and 8 tokens inside the `BooleanTree` and move to a
    `std::vector` for more. A `BooleanTree` is a little over 1 KB, so build it on the
    stack as the examples do.
  - Input up to 128 bytes is copied into the object; longer input into a heap array.
  - `BaseNode` has class-specific `operator new` and `operator delete`. Freed blocks of
    up to 64 bytes stay in a per-thread list per 16-byte size class (at most 256 per
    class, returned to the heap when the thread ends). The cache is off under
    AddressSanitizer and MemorySanitizer, and with `BOOLEAN_PARSER_NO_NODE_CACHE`.
  - `bench/bench_bool.cc` (Apple M4 Pro, Apple clang 17, `-O3`; ns per parse, median of three
    interleaved rounds under `quiet strict`; before is `e099477`):

    | workload | before | after | arena climber |
    |---|---:|---:|---:|
    | 2 terms | 156 ns | 80 ns | 56 ns |
    | 5 terms mixed | 315 ns | 196 ns | 119 ns |
    | typical query | 342 ns | 218 ns | 133 ns |
    | 16 random terms | 1.36 us | 1.04 us | 0.49 us |
    | 128 random terms | 10.8 us | 8.35 us | 4.01 us |
    | 1024 random terms | 104 us | 104 us | 33.7 us |
    | 200 nested parentheses | 2.59 us | 2.53 us | 2.94 us |

    On an Intel i9-9980HK (Apple clang 17) the same rows go 464 -> 146 ns (arena 160),
    942 -> 395 (273), 955 -> 401 (220), 3.74 -> 2.04 us (0.85), 30.0 -> 17.6 us (7.09),
    256 -> 252 us (56.6) and 6.15 -> 5.29 us (4.65).

    The precedence climber with an arena stays faster where the tree is large: it builds
    no nodes, and the cache holds 256 blocks per class, so a tree of 1,000 terms still
    pays the heap for most of its nodes. The honest remaining gap on the M4 Pro is
    1.4 to 1.7 times on short queries, 2.1 times at 16 to 128 terms and 3.1 times at 1,024
    (on the i9: parity to 1.8 times, 2.4 to 2.5 times, 4.4 times); closing it needs an arena
    owner type in the node API, which is the owner's decision.

- The oracle test runs to length 7 by default, and to length 5 (4 for the wide
  vocabulary) with 3,000 random sequences under ASan and TSan. `BP_MAX_LEN`
  overrides the length.
- `bench/bench_bool.cc` accepts that the frozen original and the library group
  equal-precedence chains differently; the oracle and differential tests check
  the results.

## 2026-10-30

### Changed

- **The lexer scans by pointer and tokens move instead of copying.** The lexer
  built a `std::string` per input byte (for an error message that is almost never
  thrown), appended each lexeme byte by byte, and copied every `Token` into the
  output list, the operator stack and the `IdNode`.
  - `NextToken` scans the NUL-terminated input by pointer, keeps the reader rule
    that `"\n\r"` and `"\r\n"` are one symbol, skips plain runs in a tight loop,
    and builds each lexeme once. The lexer does not use `ContentReader`.
  - Tokens move through the shunting-yard and into `IdNode`.
  - The RPN queue is a vector with a head index instead of a `std::list` (one
    allocation, same `front`/`back`/`pop_*` API). The operand stack is raw storage
    constructed on demand.
  - The five keywords are matched by length and first letter instead of
    `strcasecmp`.

  RPN queues, trees and exception messages are unchanged. `test/diff.cc` compares
  140,000 edge, grammar-generated and random inputs with the original sources
  (`test/frozen`, compiled with `Old`-prefixed names).

  `bench/bench_bool.cc` (`-DBOOLEAN_PARSER_BENCH=ON`; machine not recorded):

  | workload | before | after |
  |---|---|---|
  | typical query | 743 ns | 319 ns |
  | 128 random terms | 25.0 us | 10.6 us |
  | 1024 terms | 181 us | 99 us |

## 2026-10-28

### Changed

- Extracted from [Xapiand](https://github.com/Kronuz/Xapiand). `BooleanParser.h`
  drops `#include "xapian.h"` and the `Xapian::Query get_query();` declaration,
  which was never defined or called (Xapiand's `query_dsl.cc` uses only the RPN
  queue and the two exception types).

### Fixed

- `SyntacticException` owns its message as a `std::string`, like
  `LexicalException`. It borrowed a `const char*`, which dangled because every
  throw site passes `.c_str()` of a local `std::string`.
