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

### Changed

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
