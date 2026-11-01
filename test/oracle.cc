// Precedence oracle test. The reference parser in oracle.h is checked against the
// library on token sequences: every sequence up to a length over a small vocabulary,
// then random longer ones. The library must match the oracle exactly (accept or
// reject, RPN queue, tree). The frozen original (test/frozen) must match the library
// everywhere except the classes of input where its precedence differs (see
// `changed_class`); inside that class the oracle is the authority.
//
// BP_MAX_LEN raises the exhaustive length of the narrow vocabulary (default 7, or 5 under ASan/TSan; 8 and 9 take minutes).
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "oracle.h"

std::string old_dump(std::string_view input);
std::string new_dump(std::string_view input);

using oracle::Kind;
using oracle::Tok;
using oracle::Shape;
using oracle::shape_of;
using oracle::changed_class;

// Under a sanitizer every parse is an order of magnitude slower (and the CI jobs run
// every CTest case), so the default sizes shrink; BP_MAX_LEN still overrides the length.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BP_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define BP_SANITIZED 1
#endif
#endif
#ifdef BP_SANITIZED
constexpr size_t default_len = 5, wide_len = 4, random_iters = 3000;
#else
constexpr size_t default_len = 7, wide_len = 5, random_iters = 100000;
#endif

namespace {

size_t n_checked = 0, n_accepted = 0, n_changed = 0, n_truth = 0, n_old_accepted_malformed = 0;

bool has_exception(const std::string& d) { return d.find("Exception") != std::string::npos; }

[[noreturn]] void fail(const std::string& why, const std::string& in, const std::string& a, const std::string& b) {
	std::fprintf(stderr, "FAIL (%s) for input [%s]\n--- A ---\n%s\n--- B ---\n%s\n", why.c_str(), in.c_str(), a.c_str(), b.c_str());
	std::abort();
}

// ---- evaluating dumped trees, for the truth-table check ----
struct Eval {
	const std::string& s;
	size_t i = 0;
	unsigned env;  // bit 0: a, bit 1: b, bit 2: c (other ids fold onto these by their first letter)
	bool value() {
		if (s[i] == '(') {
			size_t sp = s.find(' ', i);
			std::string op = s.substr(i + 1, sp - i - 1);
			i = sp + 1;
			bool x = value();
			if (op == "NOT") { ++i; return !x; }
			++i;
			bool y = value();
			++i;
			if (op == "AND") return x && y;
			if (op == "OR") return x || y;
			if (op == "XOR") return x != y;
			std::abort();  // MAYBE has no truth table here
		}
		size_t colon = s.find(':', i);
		char id = s[colon + 1];
		size_t len = std::stoul(s.substr(i, colon - i));
		i = colon + 1 + len;
		return (env >> ((id - 'a') % 3)) & 1;
	}
};

std::string tree_of(const std::string& d) { return d.substr(d.find("TREE: ") + 6); }

bool same_truth_table(const std::string& x, const std::string& y) {
	for (unsigned env = 0; env < 8; ++env) {
		Eval ex{x, 0, env}, ey{y, 0, env};
		if (ex.value() != ey.value()) return false;
	}
	return true;
}

std::string join(const std::vector<Tok>& t) {
	std::string s;
	for (auto& k : t) { if (!s.empty()) s += ' '; s += k.text; }
	return s;
}

void check(const std::vector<Tok>& toks, bool with_old) {
	std::string in = join(toks);
	auto want = oracle::parse(toks);
	auto got = new_dump(in);
	++n_checked;
	if (want.ok == has_exception(got)) fail(want.ok ? "oracle accepts, library rejects" : "oracle rejects, library accepts", in, want.ok ? oracle::dump(want) : "(reject)", got);
	// A queue the constructor returns always parses, unless it is empty: the dump of
	// a constructed tree reads "size=N ..." and only then reports Parse()'s exception.
	if (got.rfind("size=", 0) == 0 && got.rfind("size=0 ", 0) != 0 && has_exception(got)) fail("constructor returned a queue Parse() rejects", in, "(reject)", got);
	if (want.ok) {
		++n_accepted;
		if (got != oracle::dump(want)) fail("tree/RPN differ from oracle", in, oracle::dump(want), got);
	}
	if (!with_old) return;
	auto old = old_dump(in);
	Shape sh = shape_of(toks, true), sure = shape_of(toks, false);
	// (4) Malformed input the frozen original accepts: an empty group, or a group with a
	// missing operand (`a ( NOT )`, `a () b`), whose leftovers it drops silently.
	// The oracle rejects these, and so must the library (checked above).
	if (!want.ok && !has_exception(old)) {
		++n_old_accepted_malformed;
		if (std::getenv("BP_SHOW") && n_old_accepted_malformed < 40) std::fprintf(stderr, "old accepted malformed: [%s]\n", in.c_str());
		return;
	}
	if (!changed_class(sh)) {
		// Both reject: the partial queue and the message may differ (the library's implicit
		// operator precedes the "(" that follows a term, which can change which token is
		// reported first); only the rejection has to agree.
		bool same = has_exception(old) && has_exception(got) ? true : old == got;
		if (!same) fail("old != new outside the changed classes", in, old, got);
		return;
	}
	if (old == got) {
		// Equal inside the class is only fine when both reject (nothing to change).
		if (!has_exception(got) && changed_class(sure)) fail("old == new inside the sure changed class", in, old, got);
		return;
	}
	++n_changed;
	if (!has_exception(old) && !has_exception(got) && tree_of(old) != tree_of(got)) {
		// Truth-table equivalence where the frozen result is also right: no implicit
		// operator and no MAYBE, so only the associativity of AND/OR/XOR differs.
		if (!sh.implicit && !sh.maybe) {
			++n_truth;
			if (!same_truth_table(tree_of(old), tree_of(got))) fail("associativity change altered the truth table", in, old, got);
		}
	} else if (!has_exception(old) && !has_exception(got)) {
		// Same tree, different queue: only the associativity of an identical chain.
		if (sh.implicit) fail("same tree but implicit operator present", in, old, got);
	}
}

struct Spell { Kind kind; const char* text; };

void exhaustive(const std::vector<Spell>& vocab, size_t max_len, size_t old_len) {
	for (size_t len = 1; len <= max_len; ++len) {
		std::vector<size_t> idx(len, 0);
		std::vector<Tok> toks(len);
		for (;;) {
			for (size_t k = 0; k < len; ++k) toks[k] = Tok{vocab[idx[k]].kind, vocab[idx[k]].text};
			check(toks, len <= old_len);
			size_t k = 0;
			while (k < len && ++idx[k] == vocab.size()) idx[k++] = 0;
			if (k == len) break;
		}
	}
}

}  // namespace

int main() {
	using namespace oracle;
	size_t max_len = default_len;
	if (const char* e = std::getenv("BP_MAX_LEN")) max_len = std::strtoul(e, nullptr, 10);

	// Named cases: the three reported bugs and neighbours, with the exact expected tree.
	struct Named { const char* in; const char* tree; };
	const Named named[] = {
		{"a & b c", "(OR (AND 1:a 1:b) 1:c)"},
		{"x AND a b", "(OR (AND 1:x 1:a) 1:b)"},
		{"NOT a b", "(OR (NOT 1:a) 1:b)"},
		{"a b & c", "(OR 1:a (AND 1:b 1:c))"},
		{"a OR b c", "(OR (OR 1:a 1:b) 1:c)"},
		{"a b c", "(OR (OR 1:a 1:b) 1:c)"},
		{"a & b & c", "(AND (AND 1:a 1:b) 1:c)"},
		{"a b NOT c", "(OR (OR 1:a 1:b) (NOT 1:c))"},
		{"a (b) c", "(OR (OR 1:a 1:b) 1:c)"},
		{"(a & b) c & d", "(OR (AND 1:a 1:b) (AND 1:c 1:d))"},
		{"NOT a & b c", "(OR (AND (NOT 1:a) 1:b) 1:c)"},
		{"a | b c d & e", "(OR (OR (OR 1:a 1:b) 1:c) (AND 1:d 1:e))"},
	};
	for (auto& n : named) {
		auto d = new_dump(n.in);
		auto old = old_dump(n.in);
		if (tree_of(d) != n.tree) fail("named case", n.in, n.tree, d);
		if (std::string(n.in) == "a & b c" || std::string(n.in) == "x AND a b" || std::string(n.in) == "NOT a b") {
			if (old == d) fail("old must differ on a reported bug", n.in, old, d);
		}
	}

	const std::vector<Spell> narrow = {{ID, "a"}, {ID, "b"}, {AND, "AND"}, {OR, "OR"}, {NOT, "NOT"}, {LP, "("}, {RP, ")"}};
	const std::vector<Spell> wide = {{ID, "a"}, {ID, "b"}, {AND, "AND"}, {AND, "&"}, {OR, "or"}, {OR, "|"}, {NOT, "not"}, {NOT, "!"},
		{XOR, "XOR"}, {MAYBE, "maybe"}, {LP, "("}, {RP, ")"}};
	exhaustive(narrow, max_len, 7);
	std::printf("narrow vocabulary, lengths 1..%zu: %zu inputs so far\n", max_len, n_checked);
	exhaustive(wide, wide_len, wide_len);

	// Random longer sequences: mostly well-formed shapes with implicit adjacency, some noise.
	std::mt19937 rng(20261031);
	auto pick = [&](const std::vector<Spell>& v) { return v[rng() % v.size()]; };
	const std::vector<Spell> terms = {{ID, "a"}, {ID, "b"}, {ID, "c"}, {ID, "term"}};
	const std::vector<Spell> bins = {{AND, "AND"}, {AND, "&"}, {OR, "OR"}, {OR, "|"}, {XOR, "xor"}, {MAYBE, "MAYBE"}};
	for (int iter = 0; iter < static_cast<int>(random_iters); ++iter) {
		std::vector<Tok> t;
		size_t n = 10 + rng() % 30;
		int depth = 0;
		bool noisy = rng() % 4 == 0;
		while (t.size() < n) {
			unsigned r = rng() % 100;
			if (r < 12) { auto s = pick({{NOT, "NOT"}, {NOT, "!"}}); t.push_back({s.kind, s.text}); }
			if (r < 30 && depth < 5) { t.push_back({LP, "("}); ++depth; }
			auto s = pick(terms);
			t.push_back({s.kind, s.text});
			while (depth && rng() % 4 == 0) { t.push_back({RP, ")"}); --depth; }
			if (rng() % 3) { auto b = pick(bins); t.push_back({b.kind, b.text}); }
			if (noisy && rng() % 10 == 0) { t.push_back(rng() % 2 ? Tok{RP, ")"} : Tok{AND, "AND"}); }
		}
		while (depth--) t.push_back({RP, ")"});
		if (iter % 7 == 0 && !t.empty()) t.pop_back();
		check(t, true);
	}

	std::printf("oracle OK: %zu inputs, %zu accepted; old-vs-new: %zu inputs changed inside the classes"
	            " (all others identical), %zu malformed inputs the old queue accepted, %zu truth-table checks\n", n_checked, n_accepted, n_changed, n_old_accepted_malformed, n_truth);
	return 0;
}
