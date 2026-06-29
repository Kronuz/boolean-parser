// A runnable tour of boolean-parser.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/boolean_parser_demo
//
// The one idea worth taking away: a boolean query string becomes two consumable
// forms. The constructor lexes the input and, via Dijkstra's shunting-yard,
// reorders it into an RPN (Reverse Polish notation) token queue. Parse() then
// consumes that queue to build a typed AST. Operator precedence is baked into
// the shunting-yard, so "a OR b AND c" reorders to "a b c AND OR" (AND binds
// tighter than OR) without any parentheses. This demo prints, for each
// expression, the input, the RPN token stream, and the AST drawn as a tree, so
// you can watch precedence and grouping fall out of one source string. It ends
// on a malformed input to show the parser rejecting it.
#include <cstdio>
#include <string>
#include <string_view>

#include "AndNode.h"
#include "BooleanParser.h"
#include "IdNode.h"
#include "MaybeNode.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"
#include "XorNode.h"

static void rule(const char* title) {
	std::printf("\n\033[1m-- %s --\033[0m\n", title);
}

// One RPN token as a compact symbol: operators by name, an Id by its lexeme
// (quotes and brackets preserved).
static std::string token_repr(const Token& t) {
	switch (t.get_type()) {
		case TokenType::Not:              return "NOT";
		case TokenType::Or:               return "OR";
		case TokenType::And:              return "AND";
		case TokenType::Maybe:            return "MAYBE";
		case TokenType::Xor:              return "XOR";
		case TokenType::LeftParenthesis:  return "(";
		case TokenType::RightParenthesis: return ")";
		case TokenType::Id:               return t.get_lexeme();
		case TokenType::EndOfFile:        return "<eof>";
	}
	return "?";
}

// Drain the RPN queue front-to-back into "tok tok tok". Consumes the queue, so
// this is called on its own tree (a fresh tree is built for the AST view).
static std::string rpn(BooleanTree& tree) {
	std::string out;
	while (!tree.empty()) {
		if (!out.empty()) out += ' ';
		out += token_repr(tree.front());
		tree.pop_front();
	}
	return out;
}

static const char* node_name(NodeType t) {
	switch (t) {
		case NodeType::AND:   return "AND";
		case NodeType::OR:    return "OR";
		case NodeType::NOT:   return "NOT";
		case NodeType::XOR:   return "XOR";
		case NodeType::MAYBE: return "MAYBE";
		case NodeType::ID:    return "ID";
	}
	return "?";
}

// Draw the AST as an indented tree. Binary nodes recurse into left/right, NOT
// into its single child, an ID prints its term. `prefix` carries the ASCII
// branch art so deeper levels line up under their parent.
static void draw(const BaseNode* node, const std::string& prefix, bool last) {
	const char* branch = last ? "`- " : "|- ";
	switch (node->getType()) {
		case NodeType::ID: {
			auto* id = static_cast<const IdNode*>(node);
			std::printf("%s%s%s\n", prefix.c_str(), branch, id->getId().c_str());
			return;
		}
		case NodeType::NOT: {
			auto* n = static_cast<const NotNode*>(node);
			std::printf("%s%sNOT\n", prefix.c_str(), branch);
			draw(n->getNode(), prefix + (last ? "   " : "|  "), true);
			return;
		}
		default: {
			// AND / OR / XOR / MAYBE: all share getLeftNode()/getRightNode().
			const BaseNode* left = nullptr;
			const BaseNode* right = nullptr;
			switch (node->getType()) {
				case NodeType::AND:
					left  = static_cast<const AndNode*>(node)->getLeftNode();
					right = static_cast<const AndNode*>(node)->getRightNode();
					break;
				case NodeType::OR:
					left  = static_cast<const OrNode*>(node)->getLeftNode();
					right = static_cast<const OrNode*>(node)->getRightNode();
					break;
				case NodeType::XOR:
					left  = static_cast<const XorNode*>(node)->getLeftNode();
					right = static_cast<const XorNode*>(node)->getRightNode();
					break;
				case NodeType::MAYBE:
					left  = static_cast<const MaybeNode*>(node)->getLeftNode();
					right = static_cast<const MaybeNode*>(node)->getRightNode();
					break;
				default:
					break;
			}
			std::printf("%s%s%s\n", prefix.c_str(), branch, node_name(node->getType()));
			std::string child_prefix = prefix + (last ? "   " : "|  ");
			draw(left, child_prefix, false);
			draw(right, child_prefix, true);
			return;
		}
	}
}

// Show one expression in all three forms: the raw input, the RPN token stream,
// and the AST drawn as a tree.
static void show(std::string_view expr) {
	std::printf("  input : %s\n", std::string(expr).c_str());

	BooleanTree rpn_tree(expr);
	std::printf("  RPN   : %s\n", rpn(rpn_tree).c_str());

	BooleanTree ast_tree(expr);
	ast_tree.Parse();
	std::puts("  AST   :");
	draw(ast_tree.root.get(), "          ", true);
	std::putc('\n', stdout);
}

int main() {
	std::puts("boolean-parser demo  (input -> RPN token stream -> AST)");

	// --- 1. operators and grouping -------------------------------------------
	rule("AND / OR / NOT with explicit parentheses");
	show("a OR (b AND NOT c)");

	// --- 2. precedence falls out of the shunting-yard ------------------------
	rule("precedence without parens: NOT > AND > MAYBE > XOR > OR");
	// AND binds tighter than OR, so this groups as a OR (b AND c) with no parens.
	show("a OR b AND c");
	// The whole ladder in one expression; watch where each operator lands.
	show("a OR b XOR c MAYBE d AND NOT e");

	// --- 3. the symbolic spellings and adjacent-id default-join --------------
	rule("symbolic operators (& | !) and the implicit-OR default join");
	// & is AND, ! is NOT: same tree as "a AND NOT b".
	show("a & !b");
	// Two bare terms with no operator between them default-join with OR.
	show("cat dog");

	// --- 4. quoted phrases and the [a,b,c] list are single Id tokens ---------
	rule("a quoted phrase and a [a,b,c] list each lex to one Id");
	show("\"hello world\" AND [x,y,z]");

	// --- 5. malformed input is rejected --------------------------------------
	rule("malformed input throws instead of producing a tree");
	try {
		BooleanTree bad("a AND b )");   // a stray ) with nothing to close
		(void)bad;
	} catch (const SyntacticException& e) {
		std::printf("  input : a AND b )\n  error : SyntacticException -> %s\n", e.what());
	}

	std::puts("\ndone.");
	return 0;
}
