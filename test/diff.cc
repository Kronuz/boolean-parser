// Differential test: the current lexer/parser against the frozen original on edge
// cases, a grammar-driven generator and byte soup. Compares the RPN queue (types and
// lexemes), the parse tree, and the exception kind and message.
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "BooleanLexer.h"
#include "LexicalException.h"
#include "oracle.h"

std::string old_dump(std::string_view input);
std::string new_dump(std::string_view input);

static size_t checked = 0, errors = 0, trees = 0;

static size_t deliberate = 0;
static size_t ctor_rejections = 0;

static bool has_exception(const std::string& d) { return d.find("Exception") != std::string::npos; }

// The constructor rejects a queue the original returned and then rejected in Parse(): the
// original's dump is the queue followed by Parse()'s exception, the library's is that
// exception alone (same kind, same message).
static bool constructor_rejection(const std::string& a, const std::string& b) {
	if (a.rfind("size=", 0) != 0 || b.rfind("SyntacticException: ", 0) != 0 || a.size() <= b.size()) { return false; }
	return a.compare(a.size() - b.size(), b.size(), b) == 0 && a[a.size() - b.size() - 1] == '\n' && a.find("TREE:") == std::string::npos;
}

// The library and the frozen original differ on a defined class of inputs (see oracle.h,
// `changed_class`); both lex alike, so classify by the lexer's tokens. Inside
// the class, the independent oracle decides what the right result is.
static bool deliberate_difference(const std::string& in, const std::string& b) {
	std::vector<oracle::Tok> toks;
	try {
		std::string text(in.c_str());  // the lexer stops at an embedded NUL
		Lexer lexer(text.data());
		for (;;) {
			Token t = lexer.NextToken();
			using T = TokenType;
			oracle::Kind k;
			switch (t.get_type()) {
				case T::EndOfFile: goto done;
				case T::Id: k = oracle::ID; break;
				case T::Not: k = oracle::NOT; break;
				case T::Or: k = oracle::OR; break;
				case T::And: k = oracle::AND; break;
				case T::Maybe: k = oracle::MAYBE; break;
				case T::Xor: k = oracle::XOR; break;
				case T::LeftParenthesis: k = oracle::LP; break;
				default: k = oracle::RP; break;
			}
			toks.push_back({k, t.get_lexeme()});
		}
	} catch (const LexicalException&) {
		return false;
	}
done:
	auto want = oracle::parse(toks);
	if (want.ok == has_exception(b) || (want.ok && b != oracle::dump(want))) { return false; }
	return oracle::changed_class(oracle::shape_of(toks, true)) || !want.ok;
}

static void check(const std::string& in) {
	auto a = old_dump(in);
	auto b = new_dump(in);
	++checked;
	if (has_exception(a)) { ++errors; } else { ++trees; }
	if (a != b && constructor_rejection(a, b)) { ++ctor_rejections; return; }
	if (a != b && deliberate_difference(in, b)) { ++deliberate; return; }
	if (a != b) {
		std::fprintf(stderr, "MISMATCH for input [%s] (len %zu)\n--- old ---\n%s\n--- new ---\n%s\n", in.c_str(), in.size(), a.c_str(), b.c_str());
		std::abort();
	}
}

int main() {
	std::vector<std::string> edge = {
		"", " ", "a", "a AND b", "a & b", "a | b", "!a", "NOT a", "not a", "a and b or c", "a xor b", "a maybe b",
		"a MAYBE b XOR c OR d AND NOT e", "a b c", "a OR b c", "a & b c", "x AND a b", "a b & c", "a (b) c", "a NOT b",
		"a AND", "AND", "()", "(", ")", "(a", "a)", "((a))", "NOT NOT a", "a AND NOT", "!!a", "a!b", "a&b", "a|b|c",
		"title:\"hello world\" AND (author:bob OR author:alice) AND NOT draft",
		"\"unterminated", "'unterminated", "a\\", "\"esc\\\"aped\"", "\"esc\\", "'a\\'b'", "[a,b,c]", "[\"a b\",c]", "['x',y]",
		"[a", "[a,]", "[a,b", "[\"a\"b]", "[\"a\"]x", "x[a]", "x[a,b]y", "x[\"q\"]\"r\"", "[a]\"r\"", "[a]\"r\" AND b",
		"a\tb", "a\nb", "a\rb", "a\n\rb", "a\r\nb", "a\n\r\n\rb", "a\r\r\nb", "\"x\ny\"", "\"x\n\ry\"", "\"x\r\ny\"", "\"x\n\r\n\ry\"",
		"\n\r\n a \r\n b", "a \n\r AND \r\n b", "'a\n\rb'", "[a\n\r,b]",
		"a:b", "a:\"b c\"", "a:'b c'", "a\"b c\"d", "ab\"c d\" e", "ab'c d' e", "a'\"'b", "\"'\"",
		"ORANGE", "ANDY", "oR", "aNd", "Maybe", "xOr", "NOTHING", "o", "and and and", "OR OR",
		std::string("a\0b", 3), std::string("\0", 1), "\xff\xfe", "a,b", "a]b", "]", "[", "[]", "[[a]]", "a[b]c[d]",
	};
	for (auto& e : edge) { check(e); }

	std::mt19937 rng(20260531);
	auto pick = [&](const std::vector<std::string>& v) { return v[rng() % v.size()]; };
	const std::vector<std::string> ids = {"a", "b", "foo", "x1", "bar_baz", "title:\"hello world\"", "k:'v w'", "[a,b]", "n:[\"a b\",c]",
		"\"q\"", "'s'", "w\\x", "AND_", "ORx", "notes", "a.b", "1", "0123456789abcdefghij", "0123456789abcdefghijklmnopqrstuvwxyz", "é"};
	const std::vector<std::string> ops = {"AND", "OR", "NOT", "XOR", "MAYBE", "and", "or", "not", "xor", "maybe", "&", "|", "!", "Or", "nOt"};
	const std::vector<std::string> ws = {" ", " ", " ", "  ", "\t", "\n", "\r", "\n\r", "\r\n", ""};
	for (int iter = 0; iter < 60000; ++iter) {
		std::string s;
		int n = 1 + rng() % (iter % 40 == 0 ? 60 : 10);
		int mode = rng() % 4;  // how well-formed
		for (int i = 0; i < n; ++i) {
			unsigned r = rng() % 100;
			if (r < 45) s += pick(ids);
			else if (r < 70) s += pick(ops);
			else if (r < 80) s += "(";
			else if (r < 90) s += ")";
			else if (mode >= 2) s += pick({"\"", "'", "[", "]", ",", "\\", "(", ")", "\xc3"});
			if (rng() % 8 || mode == 0) s += pick(ws);
		}
		check(s);
	}
	// Balanced, strictly alternating expressions parse; check those deeply too.
	for (int iter = 0; iter < 20000; ++iter) {
		std::string s;
		int depth = 0;
		int terms = 1 + rng() % 12;
		for (int i = 0; i < terms; ++i) {
			if (rng() % 5 == 0) s += pick({"NOT ", "!"});
			while (rng() % 4 == 0 && depth < 6) { s += "("; ++depth; }
			s += pick(ids);
			while (depth && rng() % 3 == 0) { s += ")"; --depth; }
			if (i + 1 < terms) { s += " "; if (rng() % 6) { s += pick({"AND", "OR", "XOR", "MAYBE", "&", "|"}); s += " "; } }
		}
		while (depth--) s += ")";
		check(s);
	}
	// Byte soup over the lexer's special characters.
	const std::string soup = "ab AND()&|!\"'[],\\\n\r\t:";
	for (int iter = 0; iter < 60000; ++iter) {
		std::string s(rng() % 24, ' ');
		for (auto& c : s) c = soup[rng() % soup.size()];
		check(s);
	}
	// Long lexemes and long chains.
	for (size_t n : {1u, 22u, 23u, 24u, 1023u, 1024u, 1025u, 5000u}) {
		check(std::string(n, 'z'));
		check("\"" + std::string(n, 'z') + "\" AND b");
		check("[" + std::string(n, 'z') + "] OR b");
	}
	{
		std::string chain;
		for (int i = 0; i < 3000; ++i) { chain += (i ? " OR t" : "t") + std::to_string(i); }
		check(chain);
		std::string nest(500, '(');
		check(nest + "a" + std::string(500, ')'));
		check(nest + "a" + std::string(499, ')'));
	}
	std::printf("boolean differential OK: %zu inputs (%zu parse, %zu error); %zu differ from the frozen original only inside the\n"
	            "deliberately changed precedence classes, where the library equals the oracle; %zu are rejected by the constructor\n"
	            "with the exception the original raised in Parse(); all others identical\n", checked, trees, errors, deliberate, ctor_rejections);
	return 0;
}
