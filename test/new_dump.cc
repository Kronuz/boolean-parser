// Dump with the current sources.
#include "AndNode.h"
#include "BooleanParser.h"
#include "IdNode.h"
#include "LexicalException.h"
#include "MaybeNode.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"
#include "XorNode.h"

#define DUMP_FN new_dump
#define PARSE_FN new_parse
#include "tree_dump.inc"
