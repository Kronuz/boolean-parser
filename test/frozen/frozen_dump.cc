// Dump with the frozen original sources (test/frozen/*). The CMake target renames the
// library's global names (OldLexer, OldToken, OldBooleanTree, ...) so both
// implementations can live in one executable. Includes resolve to test/frozen/.
#include "AndNode.h"
#include "BooleanParser.h"
#include "IdNode.h"
#include "LexicalException.h"
#include "MaybeNode.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"
#include "XorNode.h"

#define DUMP_FN old_dump
#define PARSE_FN old_parse
#include "../tree_dump.inc"
