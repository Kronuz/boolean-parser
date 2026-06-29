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
  target_link_libraries(boolean_parser_test PRIVATE boolean_parser)
  add_test(NAME boolean_parser COMMAND boolean_parser_test)

  # A runnable tour of the library (not a test): build & run ./build/boolean_parser_demo
  add_executable(boolean_parser_demo examples/demo.cc)
  target_link_libraries(boolean_parser_demo PRIVATE boolean_parser)
endif()
