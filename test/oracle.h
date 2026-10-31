// Independent reference for the boolean grammar: a recursive-descent parser written
// from the documented semantics, sharing no code with the library.
//
//   expr    := xor_ { (OR | <implicit>) xor_ }        lowest; adjacent terms join here
//   xor_    := maybe { XOR maybe }
//   maybe   := and_ { MAYBE and_ }
//   and_    := unary { AND unary }
//   unary   := NOT unary | "(" expr ")" | ID          tightest
//
// All binary operators are left associative. <implicit> applies when the next token
// starts an operand (a term, "(" or NOT) right after an operand. Input must end after
// one complete expression.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace oracle {

enum Kind { ID, NOT, AND, OR, XOR, MAYBE, LP, RP };

struct Tok {
	Kind kind;
	std::string text;  // the lexeme as the library reports it (ids, and operator spellings)
};

// The library's numeric TokenType values, written out here on purpose.
inline int code(Kind k) {
	switch (k) {
		case NOT: return 1;
		case OR: return 2;
		case AND: return 3;
		case MAYBE: return 4;
		case XOR: return 5;
		case ID: return 8;
		default: return 0;
	}
}

struct Result {
	bool ok = false;
	std::string tree;  // "(OR a b)" with ids as "<len>:<text>", like the library dump
	std::string rpn;   // " <code>/<len>:<text>" per token, like the library dump
	std::size_t size = 0;
	std::string back;  // "<code>:<text>" of the last RPN token
};

class Parser {
	const std::vector<Tok>& t;
	std::size_t i = 0;
	Result r;

	bool more() const { return i < t.size(); }
	bool at(Kind k) const { return more() && t[i].kind == k; }
	bool starts_operand() const { return at(ID) || at(LP) || at(NOT); }

	void emit(const Tok& tok) {
		r.rpn += " " + std::to_string(code(tok.kind)) + "/" + std::to_string(tok.text.size()) + ":" + tok.text;
		r.back = std::to_string(code(tok.kind)) + ":" + tok.text;
		++r.size;
	}

	static const char* name(Kind k) {
		switch (k) {
			case AND: return "AND";
			case OR: return "OR";
			case XOR: return "XOR";
			case MAYBE: return "MAYBE";
			default: return "NOT";
		}
	}

	bool unary(std::string& out) {
		if (at(NOT)) {
			Tok op = t[i++];
			std::string inner;
			if (!unary(inner)) return false;
			emit(op);
			out = "(NOT " + inner + ")";
			return true;
		}
		if (at(LP)) {
			++i;
			if (!expr(out)) return false;
			if (!at(RP)) return false;
			++i;
			return true;
		}
		if (at(ID)) {
			const Tok& id = t[i++];
			emit(id);
			out = std::to_string(id.text.size()) + ":" + id.text;
			return true;
		}
		return false;
	}

	// One left-associative level: operands from `next`, joined by the operator `kind`.
	template <class Next>
	bool level(std::string& out, Kind kind, Next next) {
		if (!next(out)) return false;
		while (at(kind)) {
			Tok op = t[i++];
			std::string rhs;
			if (!next(rhs)) return false;
			emit(op);
			out = std::string("(") + name(kind) + " " + out + " " + rhs + ")";
		}
		return true;
	}

	bool and_(std::string& out) { return level(out, AND, [&](std::string& o) { return unary(o); }); }
	bool maybe(std::string& out) { return level(out, MAYBE, [&](std::string& o) { return and_(o); }); }
	bool xor_(std::string& out) { return level(out, XOR, [&](std::string& o) { return maybe(o); }); }

	bool expr(std::string& out) {
		if (!xor_(out)) return false;
		while (at(OR) || starts_operand()) {
			Tok op{OR, ""};
			if (at(OR)) op = t[i++];
			std::string rhs;
			if (!xor_(rhs)) return false;
			emit(op);
			out = "(OR " + out + " " + rhs + ")";
		}
		return true;
	}

public:
	explicit Parser(const std::vector<Tok>& tokens) : t(tokens) {}

	Result run() {
		std::string out;
		r.ok = expr(out) && !more();
		r.tree = out;
		return r;
	}
};

inline Result parse(const std::vector<Tok>& tokens) { return Parser(tokens).run(); }

// What the library dump shows for an accepted input.
inline std::string dump(const Result& r) {
	return "size=" + std::to_string(r.size) + " empty=0\nback=" + r.back + "\nRPN:" + r.rpn + "\nTREE: " + r.tree;
}


// ---- the classes of input whose result the fix changes ----
// An implicit operator sits between an operand's end (a term or ")") and an operand's
// start (a term, "(" or NOT); it counts as a binary OR. The changed classes, by token
// structure alone (no use of either parser):
//  (1) associativity: in a paren group, a binary operator follows an earlier one of the
//      same level with only tighter operators between them (`a | b | c`, `a | b & c | d`).
//      The old queue grouped such chains to the right.
//  (2) implicit-operator placement: an implicit operator comes after any earlier operator
//      (binary, implicit or NOT) of the same paren group (`a & b c`, `NOT a b`). The old
//      queue emitted the next term before popping the operators that bind tighter.
//  (3) an implicit operator in front of NOT (`a NOT b`) or "(" (`a ( b ) & c`): the old
//      queue never inserted the first, and put the second inside the group.
//  (4) malformed input the old queue accepted (see check()): the oracle rejects it.
// Outside these classes the old and new results are identical. Inside, both accepted
// results must differ (the test requires it), and the new one equals the oracle.
//
// An implicit operator in front of "(" is special: the old queue emitted it inside the
// group, which is equivalent in some contexts (`a ( b ) c`) and not in others
// (`a ( b ) & c`, `a ( NOT b )`). `wide` counts it as an operator and is the class
// outside which results must be identical; `sure` ignores it and is the subset in which
// the old and new results must differ.
struct Shape {
	bool assoc = false, placement = false, before_not = false, implicit = false, maybe = false;
};

inline int level_of(Kind k) {  // looser is larger
	switch (k) {
		case oracle::AND: return 1;
		case oracle::MAYBE: return 2;
		case oracle::XOR: return 3;
		default: return 4;  // OR, implicit
	}
}

inline Shape shape_of(const std::vector<Tok>& t, bool wide) {
	struct Group { std::vector<int> open; bool any_op = false; };
	std::vector<Group> g(1);
	Shape sh;
	bool ended = false;
	auto binary = [&](int level) {
		auto& open = g.back().open;
		while (!open.empty() && open.back() < level) open.pop_back();  // tighter ones are closed
		if (!open.empty() && open.back() == level) sh.assoc = true;
		open.push_back(level);
		g.back().any_op = true;
	};
	for (size_t k = 0; k < t.size(); ++k) {
		Kind kind = t[k].kind;
		bool starts = kind == oracle::ID || kind == oracle::LP || kind == oracle::NOT;
		if (ended && starts) {
			sh.implicit = true;
			if (wide && kind == oracle::LP) sh.before_not = true;
			if (wide || kind != oracle::LP) {
				if (g.back().any_op) sh.placement = true;
				if (kind == oracle::NOT) sh.before_not = true;
				binary(level_of(oracle::OR));
			}
		}
		switch (kind) {
			case oracle::LP: g.emplace_back(); ended = false; break;
			case oracle::RP:
				if (g.size() > 1) g.pop_back();
				ended = true;
				break;
			case oracle::ID: ended = true; break;
			case oracle::NOT: g.back().any_op = true; ended = false; break;
			default:
				if (kind == oracle::MAYBE) sh.maybe = true;
				binary(level_of(kind));
				ended = false;
		}
	}
	return sh;
}

inline bool changed_class(const Shape& s) { return s.assoc || s.placement || s.before_not; }

}  // namespace oracle
