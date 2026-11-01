// boolean-parser: BooleanTree (lex + shunting-yard + Parse) against the frozen original
// (test/frozen, names prefixed "Old") and a hand-written precedence-climbing parser with an
// arena and string_view lexemes (a floor: it builds no heap nodes) that builds the same tree,
// checked for every input before it is timed. Time under a quiet hold.
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "harness.h"

const void* old_parse(std::string_view);
const void* new_parse(std::string_view);
std::string old_dump(std::string_view);
std::string new_dump(std::string_view);

// ---- comparator: precedence climbing over a tiny lexer, arena nodes ----
namespace cmp {
enum T { ID, NOT, AND, MAYBE, XOR, OR, LP, RP, END };
struct Tok { T t; std::string_view s; };
struct Node { int op; std::string_view s; int l, r; };   // op: -1 id, else T
struct P {
	std::string_view in; size_t i = 0; Tok cur; std::vector<Node> arena; bool ok = true;
	static bool ieq(std::string_view a, const char* b) { size_t n = strlen(b); if (a.size() != n) return false; for (size_t k = 0; k < n; ++k) if ((a[k] | 32) != (b[k] | 32)) return false; return true; }
	void next() {
		while (i < in.size() && (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r')) ++i;
		if (i >= in.size()) { cur = {END, {}}; return; }
		char c = in[i];
		if (c == '(') { cur = {LP, in.substr(i++, 1)}; return; }
		if (c == ')') { cur = {RP, in.substr(i++, 1)}; return; }
		if (c == '&') { cur = {AND, in.substr(i++, 1)}; return; }
		if (c == '|') { cur = {OR, in.substr(i++, 1)}; return; }
		if (c == '!') { cur = {NOT, in.substr(i++, 1)}; return; }
		size_t s = i;
		while (i < in.size()) {
			char d = in[i];
			if (d == '"' || d == '\'') { ++i; while (i < in.size() && in[i] != d) { if (in[i] == '\\') ++i; ++i; } ++i; continue; }
			if (d == '[') { ++i; while (i < in.size() && in[i] != ']') ++i; ++i; continue; }
			if (d == ' ' || d == '\n' || d == '\t' || d == '\r' || d == '(' || d == ')' || d == '&' || d == '|' || d == '!') break;
			++i;
		}
		std::string_view w = in.substr(s, i - s);
		T t = ID;
		if (ieq(w, "and")) t = AND; else if (ieq(w, "or")) t = OR; else if (ieq(w, "not")) t = NOT; else if (ieq(w, "xor")) t = XOR; else if (ieq(w, "maybe")) t = MAYBE;
		cur = {t, w};
	}
	int mk(int op, std::string_view s, int l, int r) { arena.push_back({op, s, l, r}); return int(arena.size()) - 1; }
	static int prec(T t) { switch (t) { case AND: return 1; case MAYBE: return 2; case XOR: return 3; case OR: return 4; default: return 9; } }
	int unary() {
		if (cur.t == NOT) { next(); int c = unary(); return mk(NOT, {}, c, -1); }
		if (cur.t == LP) { next(); int e = expr(4); if (cur.t != RP) { ok = false; return 0; } next(); return e; }
		if (cur.t == ID) { auto s = cur.s; next(); return mk(-1, s, -1, -1); }
		ok = false; return 0;
	}
	// binary ops left-assoc; implicit OR when an operand is directly followed by an Id.
	int expr(int maxprec) {
		int lhs = unary();
		for (;;) {
			T op = cur.t; bool implicit = false;
			if (op == ID) { op = OR; implicit = true; }
			if (op != AND && op != MAYBE && op != XOR && op != OR) break;
			int p = prec(op);
			if (p > maxprec) break;
			if (!implicit) next();
			int rhs = expr(p - 1);   // an operator of the same precedence ends the right operand: left associative
			lhs = mk(op, {}, lhs, rhs);
		}
		return lhs;
	}
};
static void dump(const P& p, int n, std::string& o) {
	const Node& x = p.arena[n];
	if (x.op == -1) { o += std::to_string(x.s.size()) + ":" + std::string(x.s); return; }
	if (x.op == NOT) { o += "(NOT "; dump(p, x.l, o); o += ')'; return; }
	o += x.op == AND ? "(AND " : x.op == OR ? "(OR " : x.op == XOR ? "(XOR " : "(MAYBE ";
	dump(p, x.l, o); o += ' '; dump(p, x.r, o); o += ')';
}
}  // namespace cmp

static std::string gen(int terms, std::mt19937& rng, bool quoted) {
	static const char* ops[] = {" AND ", " OR ", " XOR ", " MAYBE ", " & ", " | ", " "};
	std::string s; int depth = 0;
	for (int i = 0; i < terms; ++i) {
		bool neg = rng() % 8 == 0, par = rng() % 6 == 0 && depth < 4;
		if (i) { const char* o = ops[rng() % 7]; if (!strcmp(o, " ")) o = " OR "; s += o; }  /* implicit OR is spelled out: the frozen original places an implicit one differently */
		if (neg) s += "NOT ";
		if (par) { s += '('; ++depth; }
		if (quoted && rng() % 5 == 0) s += "field:\"quoted value " + std::to_string(i) + "\"";
		else s += "term" + std::to_string(rng() % 1000);
		if (depth && rng() % 4 == 0) { s += ')'; --depth; }
	}
	while (depth--) s += ')';
	return s;
}

int main() {
	std::mt19937 rng(11);
	std::vector<std::pair<std::string, std::string>> inputs = {
		{"2 terms", "alpha AND beta"},
		{"5 terms mixed", "(foo OR bar) AND NOT baz XOR qux"},
		{"typical query", "title:\"hello world\" AND (author:bob OR author:alice) AND NOT draft"},
	};
	for (int t : {16, 128, 1024}) inputs.push_back({std::to_string(t) + " terms random", gen(t, rng, t != 1024)});
	inputs.push_back({"deep nest 200", std::string(200, '(') + "x" + std::string(200, ')')});
	for (auto& [name, q] : inputs) {
		// The climber must build the very tree BooleanTree builds: its dump is compared with the
		// library's before anything is timed. The frozen original groups chains of one operator
		// to the right, so only whether it parses is compared with it.
		{
			cmp::P p{q}; p.next(); int r = p.expr(4);
			std::string tree, lib = new_dump(q);
			if (p.ok) { cmp::dump(p, r, tree); }
			auto at = lib.find("TREE: ");
			if (!p.ok || p.cur.t != cmp::END || at == std::string::npos || lib.substr(at + 6) != tree) { std::printf("CLIMBER MISMATCH %s\n", name.c_str()); return 1; }
		}
		if ((old_dump(q).find("Exception") == std::string::npos) != (new_dump(q).find("Exception") == std::string::npos)) { std::printf("MISMATCH %s\n", name.c_str()); return 1; }
		auto fo = [&](uint64_t n) { for (uint64_t i = 0; i < n; ++i) { bh::use(old_parse(q)); } };
		auto fn = [&](uint64_t n) { for (uint64_t i = 0; i < n; ++i) { bh::use(new_parse(q)); } };
		auto fc = [&](uint64_t n) { for (uint64_t i = 0; i < n; ++i) { cmp::P p{q}; p.arena.reserve(q.size() / 2 + 4); p.next(); int r = p.expr(4); bh::use(r); bh::use(p.arena); } };
		bh::row("bool", name.c_str(), "current #1", bh::measure(fn), q.size());
		bh::row("bool", name.c_str(), "current #2 (A/A)", bh::measure(fn), q.size());
		bh::row("bool", name.c_str(), "frozen old", bh::measure(fo), q.size());
		bh::row("bool", name.c_str(), "precedence-climb arena", bh::measure(fc), q.size());
	}
}
