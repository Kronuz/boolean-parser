// Smoke test for the standalone boolean-parser library.
//
// The public surface is the BooleanTree class (BooleanParser.h):
//
//   1. The constructor BooleanTree(std::string_view) lexes the input and, via
//      Dijkstra's shunting-yard, builds an RPN (Reverse Polish notation) token
//      queue. That queue is the most directly testable surface, exposed through
//      empty() / size() / front() / back() / pop_front() / pop_back(), each
//      element a Token with get_type() (TokenType) and get_lexeme().
//   2. Parse() consumes the RPN queue to build the AST into `root` (a tree of
//      BaseNode subclasses: And/Or/Not/Xor/Maybe/IdNode).
//
// So the tests drive both: they assert on the RPN token stream for a range of
// expressions (operators, parentheses, quoting, the [a,b,c] list syntax, and
// operator precedence), confirm Parse() yields the right AST shape, and check
// that malformed inputs throw LexicalException / SyntacticException.
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "AndNode.h"
#include "BooleanParser.h"
#include "IdNode.h"
#include "LexicalException.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"


// Render one RPN token as a compact symbol: operators as their name, an Id as
// its lexeme. Used to flatten the whole RPN queue into a single space-separated
// string we can assert against.
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

// Drain the RPN queue front-to-back into "tok tok tok". Consumes the tree's
// queue (pop_front), exactly as a real consumer would.
static std::string rpn(BooleanTree& tree) {
	std::string out;
	while (!tree.empty()) {
		if (!out.empty()) out += ' ';
		out += token_repr(tree.front());
		tree.pop_front();
	}
	return out;
}

static std::string rpn_of(std::string_view expr) {
	BooleanTree tree(expr);
	return rpn(tree);
}


// ---------------------------------------------------------------------------
// 1. Basic operators and parentheses -> RPN token stream.
// ---------------------------------------------------------------------------

static void test_basic_rpn() {
	// a AND b  ->  a b AND
	assert(rpn_of("a AND b") == "a b AND");

	// The symbolic spelling (&) lexes to the same operator.
	assert(rpn_of("a & b") == "a b AND");

	// a OR (b AND NOT c)  ->  a b c NOT AND OR
	// NOT binds tightest, then AND; the parens are already respected by the
	// natural precedence here but exercise the paren path in the lexer/RPN.
	assert(rpn_of("a OR (b AND NOT c)") == "a b c NOT AND OR");

	// NOT is unary and highest precedence.
	assert(rpn_of("NOT a") == "a NOT");

	std::printf("rpn OK: basic operators and parentheses\n");
}


// ---------------------------------------------------------------------------
// 2. Operator precedence: NOT > AND > MAYBE > XOR > OR (NOT binds tightest).
// ---------------------------------------------------------------------------

static void test_precedence() {
	// a OR b AND c : AND binds tighter than OR  ->  a b c AND OR
	assert(rpn_of("a OR b AND c") == "a b c AND OR");

	// a AND b OR c : same precedence relation, AND first  ->  a b AND c OR
	assert(rpn_of("a AND b OR c") == "a b AND c OR");

	// a XOR b OR c : XOR binds tighter than OR  ->  a b XOR c OR
	assert(rpn_of("a XOR b OR c") == "a b XOR c OR");

	// a OR b MAYBE c : MAYBE binds tighter than OR  ->  a b c MAYBE OR
	assert(rpn_of("a OR b MAYBE c") == "a b c MAYBE OR");

	// Full ladder NOT > AND > MAYBE > XOR > OR in one expression:
	//   a OR b XOR c MAYBE d AND NOT e
	// NOT e  -> e NOT ; d AND (e NOT) -> d e NOT AND ; MAYBE binds c with that
	// -> c d e NOT AND MAYBE ; XOR binds b with that -> b c d e NOT AND MAYBE XOR ;
	// OR is loosest -> a b c d e NOT AND MAYBE XOR OR
	assert(rpn_of("a OR b XOR c MAYBE d AND NOT e") ==
	       "a b c d e NOT AND MAYBE XOR OR");

	std::printf("rpn OK: precedence NOT > AND > MAYBE > XOR > OR\n");
}


// ---------------------------------------------------------------------------
// 3. Implicit-AND-equivalent: two adjacent ids default-join with OR (the
//    DEFAULT_OPERATOR), so "a b" parses without an explicit operator.
// ---------------------------------------------------------------------------

static void test_adjacent_ids() {
	// Two bare terms in a row are joined by the default operator (OR).
	assert(rpn_of("a b") == "a b OR");
	std::printf("rpn OK: adjacent ids default-join with OR\n");
}


// ---------------------------------------------------------------------------
// 4. Quoting: a double- or single-quoted phrase is one Id whose lexeme keeps
//    the quotes and the embedded space.
// ---------------------------------------------------------------------------

static void test_quoting() {
	{
		BooleanTree tree("\"hello world\"");
		assert(tree.size() == 1);
		assert(tree.front().get_type() == TokenType::Id);
		assert(tree.front().get_lexeme() == "\"hello world\"");
	}
	{
		// Quoted phrase as an operand: "hello world" AND b  ->  <phrase> b AND
		assert(rpn_of("\"hello world\" AND b") == "\"hello world\" b AND");
	}
	{
		// Single quotes work too.
		BooleanTree tree("'a b c'");
		assert(tree.size() == 1);
		assert(tree.front().get_lexeme() == "'a b c'");
	}
	std::printf("rpn OK: quoted phrases are a single Id token\n");
}


// ---------------------------------------------------------------------------
// 5. The [a,b,c] list syntax: a bracketed, comma-separated list lexes to a
//    single Id token whose lexeme is the whole "[a,b,c]".
// ---------------------------------------------------------------------------

static void test_list_syntax() {
	{
		BooleanTree tree("[x,y,z]");
		assert(tree.size() == 1);
		assert(tree.front().get_type() == TokenType::Id);
		assert(tree.front().get_lexeme() == "[x,y,z]");
	}
	{
		// A list as an operand: [x,y,z] AND w  ->  [x,y,z] w AND
		assert(rpn_of("[x,y,z] AND w") == "[x,y,z] w AND");
	}
	std::printf("rpn OK: [a,b,c] list syntax is a single Id token\n");
}


// ---------------------------------------------------------------------------
// 6. Parse(): the RPN queue builds the expected AST shape into `root`.
// ---------------------------------------------------------------------------

static void test_parse_ast() {
	{
		// A single term parses to one IdNode.
		BooleanTree tree("hello");
		tree.Parse();
		assert(tree.root != nullptr);
		assert(tree.root->getType() == NodeType::ID);
		assert(dynamic_cast<IdNode*>(tree.root.get())->getId() == "hello");
	}
	{
		// a AND b parses to an AndNode over two IdNodes.
		BooleanTree tree("a AND b");
		tree.Parse();
		assert(tree.root != nullptr);
		assert(tree.root->getType() == NodeType::AND);
		auto* andn = dynamic_cast<AndNode*>(tree.root.get());
		assert(andn != nullptr);
		assert(andn->getLeftNode()->getType() == NodeType::ID);
		assert(andn->getRightNode()->getType() == NodeType::ID);
	}
	{
		// a OR (b AND NOT c): root OR, with a NOT somewhere under the AND child.
		BooleanTree tree("a OR (b AND NOT c)");
		tree.Parse();
		assert(tree.root != nullptr);
		assert(tree.root->getType() == NodeType::OR);
		auto* orn = dynamic_cast<OrNode*>(tree.root.get());
		assert(orn != nullptr);
		// One side is the bare id `a`, the other is the AND subtree.
		bool has_id = orn->getLeftNode()->getType() == NodeType::ID ||
		              orn->getRightNode()->getType() == NodeType::ID;
		bool has_and = orn->getLeftNode()->getType() == NodeType::AND ||
		               orn->getRightNode()->getType() == NodeType::AND;
		assert(has_id && has_and);
	}
	std::printf("parse OK: AST shape matches for id / AND / nested OR\n");
}


// ---------------------------------------------------------------------------
// 7. Malformed inputs throw.
// ---------------------------------------------------------------------------

static void test_errors() {
	// Unterminated quote -> LexicalException ("double quote expected").
	{
		bool threw = false;
		try {
			BooleanTree tree("\"unterminated");
		} catch (const LexicalException&) {
			threw = true;
		}
		assert(threw);
	}

	// A bare ']' with no matching '[' inside a bracket list is a lexical error.
	// "[a" never closes the bracket -> the lexer hits EOF expecting ']'.
	{
		bool threw = false;
		try {
			BooleanTree tree("[a");
		} catch (const LexicalException&) {
			threw = true;
		}
		assert(threw);
	}

	// Unbalanced ')' with nothing to close -> SyntacticException (") was
	// expected"). Thrown from the RPN build in the ctor.
	{
		bool threw = false;
		try {
			BooleanTree tree("a )");
		} catch (const SyntacticException&) {
			threw = true;
		}
		assert(threw);
	}

	// A stray ')' after a complete subexpression is likewise a SyntacticException
	// from the ctor's RPN build. This also exercises the hardened
	// SyntacticException: its message is built from a temporary std::string, so a
	// bare-const-char* implementation would dangle when .what() is read here.
	{
		bool threw = false;
		std::string msg;
		try {
			BooleanTree tree("a AND b )");
		} catch (const SyntacticException& e) {
			threw = true;
			msg = e.what();   // reads owned storage; must stay valid post-throw
		}
		assert(threw);
		assert(!msg.empty());
	}

	// Dangling operators (e.g. "a AND AND b", "a AND", "AND a") produce an RPN
	// with more operators than operands. The tree builder in Parse() used to
	// recurse onto an emptied output list and dereference .back() on an empty
	// list (undefined behavior -> abort); it now throws SyntacticException. The
	// ctor succeeds (the RPN is well-formed as a token stream); Parse() is what
	// rejects it, so the throw is exercised there.
	for (const char* bad : {"a AND AND b", "a AND", "AND a", "a OR OR b"}) {
		bool threw = false;
		try {
			BooleanTree tree(bad);
			tree.Parse();
		} catch (const SyntacticException&) {
			threw = true;
		}
		assert(threw);
	}

	std::printf("errors OK: malformed inputs throw Lexical/Syntactic exceptions\n");
}


int main() {
	test_basic_rpn();
	test_precedence();
	test_adjacent_ids();
	test_quoting();
	test_list_syntax();
	test_parse_ast();
	test_errors();
	std::printf("all boolean-parser tests passed\n");
	return 0;
}
