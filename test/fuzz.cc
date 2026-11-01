// libFuzzer target for BooleanTree, with a grammar-aware mutator.
//
// Build (clang): cmake -B build-fuzz -DBOOLEAN_PARSER_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ \
//                      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1"
//                && cmake --build build-fuzz --target boolean_parser_fuzz
// Run:   build-fuzz/boolean_parser_fuzz -max_len=2048 -dict=test/fuzz.dict corpus/
//
// For every input the target checks, against code that shares nothing with the library:
//  1. The constructor throws LexicalException or SyntacticException, or returns a queue that is
//     empty or one well-formed RPN expression (a stack machine written here accepts it).
//  2. A non-empty queue always parses, and the tree and the RPN dump equal the reference
//     parser's (test/oracle.h), fed with the library's own lexer tokens; a rejected input is
//     rejected by both.
//  3. PrintTree() walks the tree of every input it can (bounded depth) without a crash.
// The mutator edits at the level of tokens (insert, delete, replace, duplicate, swap, wrap in
// parentheses, unbalance them) so most mutants stay close to the grammar, and starts from
// generated expressions, with plain byte mutations mixed in.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AndNode.h"
#include "BooleanLexer.h"
#include "BooleanParser.h"
#include "IdNode.h"
#include "LexicalException.h"
#include "MaybeNode.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"
#include "XorNode.h"
#include "oracle.h"

std::string new_dump(std::string_view input);

extern "C" size_t LLVMFuzzerMutate(uint8_t* data, size_t size, size_t max_size);

namespace {

[[noreturn]] void fail(const char* why, std::string_view input, const std::string& detail) {
	std::fprintf(stderr, "FUZZ FAILURE (%s) for input of %zu bytes:\n", why, input.size());
	std::fwrite(input.data(), 1, input.size(), stderr);
	std::fprintf(stderr, "\n%s\n", detail.c_str());
	std::abort();
}

// An independent check of a queue: operators find their operands and one value remains.
bool well_formed(BooleanTree& tree) {
	std::size_t depth = 0;
	while (!tree.empty()) {
		switch (tree.front().get_type()) {
			case TokenType::Id: ++depth; break;
			case TokenType::Not: if (depth < 1) return false; break;
			case TokenType::Or:
			case TokenType::And:
			case TokenType::Maybe:
			case TokenType::Xor: if (depth < 2) return false; --depth; break;
			default: return false;
		}
		tree.pop_front();
	}
	return depth <= 1;
}

std::size_t tree_depth(BaseNode* node) {
	// Iterative, bounded: the caller only needs to know whether the tree is shallow.
	std::size_t deepest = 0;
	std::vector<std::pair<BaseNode*, std::size_t>> todo{{node, 1}};
	while (!todo.empty() && deepest <= 100) {
		auto [n, d] = todo.back();
		todo.pop_back();
		deepest = std::max(deepest, d);
		switch (n->getType()) {
			case NodeType::NOT: todo.push_back({static_cast<NotNode*>(n)->getNode(), d + 1}); break;
			case NodeType::AND: todo.push_back({static_cast<AndNode*>(n)->getLeftNode(), d + 1}); todo.push_back({static_cast<AndNode*>(n)->getRightNode(), d + 1}); break;
			case NodeType::OR: todo.push_back({static_cast<OrNode*>(n)->getLeftNode(), d + 1}); todo.push_back({static_cast<OrNode*>(n)->getRightNode(), d + 1}); break;
			case NodeType::XOR: todo.push_back({static_cast<XorNode*>(n)->getLeftNode(), d + 1}); todo.push_back({static_cast<XorNode*>(n)->getRightNode(), d + 1}); break;
			case NodeType::MAYBE: todo.push_back({static_cast<MaybeNode*>(n)->getLeftNode(), d + 1}); todo.push_back({static_cast<MaybeNode*>(n)->getRightNode(), d + 1}); break;
			case NodeType::ID: break;
		}
	}
	return deepest;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	static const bool silenced = [] { std::cout.setstate(std::ios::badbit); return true; }();
	(void)silenced;
	if (size > 4096) { return 0; }
	const std::string_view input(reinterpret_cast<const char*>(data), size);

	// 1. The constructor: an exception, or a well-formed queue.
	bool constructed = false;
	std::size_t queue_size = 0;
	try {
		BooleanTree tree(input);
		queue_size = tree.size();
		if (!well_formed(tree)) { fail("constructor returned a malformed queue", input, new_dump(input)); }
		constructed = true;
	} catch (const LexicalException&) {
	} catch (const SyntacticException&) {
	}
	bool parsed = false;
	try {
		BooleanTree tree(input);
		tree.Parse();
		parsed = true;
	} catch (const LexicalException&) {
	} catch (const SyntacticException&) {
	}

	// 2. The reference parser over the library's own tokens.
	std::vector<oracle::Tok> toks;
	bool lexed = true;
	try {
		std::string buffer(input.substr(0, input.find('\0')));  // the lexer stops at a NUL
		Lexer lexer(buffer.data());
		for (;;) {
			Token t = lexer.NextToken();
			oracle::Kind k;
			switch (t.get_type()) {
				case TokenType::EndOfFile: goto done;
				case TokenType::Id: k = oracle::ID; break;
				case TokenType::Not: k = oracle::NOT; break;
				case TokenType::Or: k = oracle::OR; break;
				case TokenType::And: k = oracle::AND; break;
				case TokenType::Maybe: k = oracle::MAYBE; break;
				case TokenType::Xor: k = oracle::XOR; break;
				case TokenType::LeftParenthesis: k = oracle::LP; break;
				default: k = oracle::RP; break;
			}
			toks.push_back({k, t.get_lexeme()});
		}
	} catch (const LexicalException&) {
		lexed = false;
	}
done:
	if (!lexed) {
		// A syntactic error earlier in the text is reported before the lexer reaches the lexical one.
		if (parsed) { fail("lexical error not reported", input, new_dump(input)); }
		return 0;
	}
	const auto want = oracle::parse(toks);
	if (want.ok != parsed) { fail(want.ok ? "oracle accepts, library rejects" : "oracle rejects, library accepts", input, new_dump(input)); }
	if (want.ok) {
		const std::string got = new_dump(input);
		if (got != oracle::dump(want)) { fail("tree or queue differ from the oracle", input, got + "\n--- oracle ---\n" + oracle::dump(want)); }
	}
	if (constructed && queue_size != 0 && !parsed) { fail("constructor returned a queue Parse() rejects", input, new_dump(input)); }

	// 3. Walk the tree with PrintTree() when it is shallow enough for its recursion.
	if (want.ok) {
		BooleanTree tree(input);
		tree.Parse();
		if (tree_depth(tree.root.get()) <= 100) { tree.PrintTree(); }
	}
	return 0;
}


namespace {

const std::string_view vocabulary[] = {
	"a", "b", "c", "term", "foo:bar", "x1", "\"q w\"", "'q'", "[x,y]", "k:[\"a b\",c]", "AND", "OR", "NOT", "XOR", "MAYBE",
	"and", "or", "not", "xor", "maybe", "&", "|", "!", "(", ")", "(", ")", " ", "  ", "\t", "\n", "\r", "\n\r", "\r\n", "\\", "\"",
	"'", "[", "]", ",", "\xc3\xa9", std::string_view("\0", 1), "ORANGE", "NOTHING", "a\\\"b", "((", "))", "()", "NOT NOT",
};
constexpr std::size_t vocabulary_size = sizeof(vocabulary) / sizeof(vocabulary[0]);

std::string generate(std::mt19937& rng, int depth) {
	std::string s;
	const int terms = 1 + static_cast<int>(rng() % 4);
	for (int i = 0; i < terms; ++i) {
		if (i) {
			static const char* const ops[] = {" AND ", " OR ", " XOR ", " MAYBE ", " & ", " | ", " "};
			s += ops[rng() % 7];
		}
		if (rng() % 5 == 0) { s += rng() % 2 ? "NOT " : "!"; }
		if (depth > 0 && rng() % 3 == 0) { s += "(" + generate(rng, depth - 1) + ")"; }
		else { s += std::string(vocabulary[rng() % 7]); }
	}
	return s;
}

// Positions where a token starts or ends: around spaces and parentheses.
std::vector<std::size_t> boundaries(const std::string& s) {
	std::vector<std::size_t> at{0};
	for (std::size_t i = 0; i < s.size(); ++i) {
		if (s[i] == ' ' || s[i] == '(' || s[i] == ')') { at.push_back(i); at.push_back(i + 1); }
	}
	at.push_back(s.size());
	return at;
}

}  // namespace

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t* data, size_t size, size_t max_size, unsigned seed) {
	std::mt19937 rng(seed);
	std::string s(reinterpret_cast<char*>(data), size);
	if (s.empty() || rng() % 16 == 0) {
		s = generate(rng, 3);
	}
	const int rounds = 1 + static_cast<int>(rng() % 3);
	for (int r = 0; r < rounds; ++r) {
		const auto at = boundaries(s);
		auto pick = [&] { return at[rng() % at.size()]; };
		auto token = [&] { return std::string(vocabulary[rng() % vocabulary_size]); };
		auto span = [&] {
			std::size_t from = pick(), to = pick();
			if (from > to) { std::swap(from, to); }
			return std::pair<std::size_t, std::size_t>{from, to - from};
		};
		switch (rng() % 10) {
			case 0: s.insert(pick(), " " + token() + " "); break;
			case 1: s.insert(pick(), token()); break;
			case 2: { auto [from, len] = span(); s.erase(from, len); break; }
			case 3: { auto [from, len] = span(); s.replace(from, len, token()); break; }
			case 4: { auto [from, len] = span(); s.insert(pick(), s.substr(from, len)); break; }
			case 5: {  // swap two neighbouring spans
				auto [from, len] = span();
				const std::size_t other = std::min(s.size(), from + len + (rng() % 8));
				if (other > from + len) { std::string a = s.substr(from, len), b = s.substr(from + len, other - from - len); s.replace(from, other - from, b + a); }
				break;
			}
			case 6: { auto [from, len] = span(); s.insert(from + len, ")"); s.insert(from, "("); break; }
			case 7: {  // unbalance: drop or add one parenthesis
				const std::size_t p = s.find_first_of("()", pick() % (s.size() + 1));
				if (p != std::string::npos && rng() % 2) { s.erase(p, 1); } else { s.insert(pick(), rng() % 2 ? "(" : ")"); }
				break;
			}
			case 8: { auto [from, len] = span(); s.insert(from + len, " " + std::string(rng() % 2 ? "AND" : "OR") + " "); break; }
			default: {  // a plain byte mutation
				std::string copy = s;
				copy.resize(std::max<std::size_t>(max_size, copy.size()));
				const std::size_t n = LLVMFuzzerMutate(reinterpret_cast<uint8_t*>(copy.data()), s.size(), copy.size());
				copy.resize(n);
				s = copy;
				break;
			}
		}
	}
	if (s.size() > max_size) { s.resize(max_size); }
	std::copy(s.begin(), s.end(), data);
	return s.size();
}
