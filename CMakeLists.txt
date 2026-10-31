cmake_minimum_required(VERSION 3.14)
project(boolean_parser LANGUAGES CXX)

# --- Library target ---
# A small STATIC library carrying the lexer/parser translation units. The
# parser is split across a handful of co-located headers and three .cc files
# (BooleanParser.cc, BooleanLexer.cc, ContentReader.cc) that include each other
# with bare paths ("BooleanLexer.h", "BooleanNode.h", ...). The three most
# generic names (Lexer/Node/Token) carry a "Boolean" prefix so the headers can
# sit at the repo root without colliding with a consumer's own Node.h/Token.h.
# The repo root is the PUBLIC include directory, so consumers reach the public
# header as "BooleanParser.h" and the internal bare includes resolve because the
# sources sit side by side.
file(GLOB BOOLEAN_PARSER_SRC_LIST
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
add_library(boolean_parser STATIC ${BOOLEAN_PARSER_SRC_LIST})
target_include_directories(boolean_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The code uses C++17 (std::string_view, [[fallthrough]]) and nothing newer, so
# request cxx_std_17 for the widest portability.
target_compile_features(boolean_parser PUBLIC cxx_std_17)
# Alias so consumers can link `boolean_parser::boolean_parser` whether they add
# this via add_subdirectory or FetchContent.
add_library(boolean_parser::boolean_parser ALIAS boolean_parser)

# Only build the test when this repo is the top-level project, so consumers
# (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(boolean_parser_test test/test.cc)
  find_package(Threads REQUIRED)
  target_link_libraries(boolean_parser_test PRIVATE boolean_parser Threads::Threads)
  add_test(NAME boolean_parser COMMAND boolean_parser_test)

  # Differential test against the frozen original (test/frozen). The frozen sources are
  # compiled with every global name prefixed "Old" so both implementations can share one
  # executable; the test compares RPN queue, tree and exception text on edge cases and
  # random input.
  set(BP_RENAMES Symbol ContentReader Lexer LexerState Token TokenType NodeType BaseNode AndNode
    MaybeNode NotNode OrNode XorNode IdNode LexicalException SyntacticException BooleanTree)
  set(BP_OLD_DEFS "")
  foreach(n ${BP_RENAMES})
    list(APPEND BP_OLD_DEFS "${n}=Old${n}")
  endforeach()
  add_library(boolean_parser_frozen OBJECT
    test/frozen/BooleanLexer.cc test/frozen/BooleanParser.cc test/frozen/ContentReader.cc
    test/frozen/frozen_dump.cc)
  target_compile_definitions(boolean_parser_frozen PRIVATE ${BP_OLD_DEFS})
  target_compile_features(boolean_parser_frozen PRIVATE cxx_std_17)
  add_executable(boolean_parser_diff test/diff.cc test/new_dump.cc $<TARGET_OBJECTS:boolean_parser_frozen>)
  target_include_directories(boolean_parser_diff PRIVATE test)
  target_link_libraries(boolean_parser_diff PRIVATE boolean_parser)
  add_test(NAME boolean_parser_diff COMMAND boolean_parser_diff)

  # Precedence oracle: an independent recursive-descent reference parser (test/oracle.h)
  # against the library on exhaustive and random token sequences, and the frozen original
  # against the library outside the classes the precedence fix changes.
  add_executable(boolean_parser_oracle test/oracle.cc test/new_dump.cc $<TARGET_OBJECTS:boolean_parser_frozen>)
  target_include_directories(boolean_parser_oracle PRIVATE test)
  target_link_libraries(boolean_parser_oracle PRIVATE boolean_parser)
  add_test(NAME boolean_parser_oracle COMMAND boolean_parser_oracle)

  # Benchmark: cmake -B build -DBOOLEAN_PARSER_BENCH=ON -DCMAKE_BUILD_TYPE=Release
  #            && cmake --build build --target boolean_parser_bench
  option(BOOLEAN_PARSER_BENCH "Build the parser benchmark" OFF)
  if(BOOLEAN_PARSER_BENCH)
    add_executable(boolean_parser_bench bench/bench_bool.cc test/new_dump.cc $<TARGET_OBJECTS:boolean_parser_frozen>)
    target_include_directories(boolean_parser_bench PRIVATE bench test)
    target_link_libraries(boolean_parser_bench PRIVATE boolean_parser)
  endif()

  # A runnable tour of the library (not a test): build & run ./build/boolean_parser_demo
  add_executable(boolean_parser_demo examples/demo.cc)
  target_link_libraries(boolean_parser_demo PRIVATE boolean_parser)
endif()
