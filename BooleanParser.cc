/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2018 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "BooleanParser.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <utility>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "AndNode.h"
#include "IdNode.h"
#include "MaybeNode.h"
#include "NotNode.h"
#include "OrNode.h"
#include "SyntacticException.h"
#include "XorNode.h"


constexpr auto DEFAULT_OPERATOR = TokenType::Or;


namespace {
// The lexer reads a NUL-terminated copy of the input; an embedded NUL ends the text.
std::unique_ptr<char[]>
copy_input(std::string_view input)
{
	auto copy = std::make_unique<char[]>(input.size() + 1);
	std::memcpy(copy.get(), input.data(), input.size());
	copy[input.size()] = '\0';
	return copy;
}
}  // namespace


BooleanTree::BooleanTree(std::string_view input_)
	: input(copy_input(input_)),
	  lexer(input.get())
{
	// Typical queries have a handful of tokens: reserve once instead of growing.
	stack_output.reserve(16);
	stack_operator.reserve(8);
	toRPN();
}


namespace {

// Trees at most this deep are destroyed by ordinary recursion: about 70 KiB of stack
// unoptimized under ASan, well inside a 512 KiB thread. Deeper ones are torn down
// iteratively.
constexpr std::size_t recursive_destroy_depth = 64;

// Operands built so far: each subtree, its depth, and the text of the token that
// produced it (for error messages; the text lives in the node or the output queue, both
// of which stay put). Tears deep subtrees down iteratively if
// parsing throws.
struct Operand {
	std::unique_ptr<BaseNode> node;
	std::size_t depth = 0;
	const std::string* head = nullptr;
};

// A stack of operands: the first 32 live inline (in raw storage, so nothing is
// constructed until it is used), so a typical query allocates nothing here; longer
// ones spill to the heap.
class Operands {
	static constexpr std::size_t inline_capacity = 32;
	alignas(Operand) unsigned char raw[inline_capacity * sizeof(Operand)];
	std::vector<Operand> spill;
	std::size_t count = 0;

	Operand* local() noexcept { return reinterpret_cast<Operand*>(raw); }

public:
	Operands() = default;
	Operands(const Operands&) = delete;
	Operands& operator=(const Operands&) = delete;

	~Operands() {
		for (std::size_t i = 0; i < count; ++i) {
			auto& item = at(i);
			if (item.depth > recursive_destroy_depth) {
				destroy_tree(std::move(item.node));
			}
			if (i < inline_capacity) {
				item.~Operand();
			}
		}
	}

	std::size_t size() const noexcept { return count; }

	Operand& at(std::size_t i) { return i < inline_capacity ? local()[i] : spill[i - inline_capacity]; }

	void need(std::size_t n) const {
		if (count < n) {
			throw SyntacticException("Unexpected end of expression");
		}
	}

	void push(Operand item) {
		if (count < inline_capacity) {
			new (&local()[count]) Operand(std::move(item));
		} else {
			spill.push_back(std::move(item));
		}
		++count;
	}

	Operand pop() {
		--count;
		if (count < inline_capacity) {
			Operand item(std::move(local()[count]));
			local()[count].~Operand();
			return item;
		}
		auto item = std::move(spill.back());
		spill.pop_back();
		return item;
	}
};

}  // namespace


void
BooleanTree::Parse()
{
	release_root();
	root = BuildTree();
}


void
BooleanTree::release_root()
{
	if (root_depth > recursive_destroy_depth) {
		destroy_tree(std::move(root));
	}
	root.reset();
	root_depth = 0;
}


std::unique_ptr<BaseNode>
BooleanTree::BuildTree()
{
	/* Consumes the RPN queue front to back with an explicit operand stack, so nesting
	 * depth is bounded by the input's length, not the native stack, and a binary
	 * operator's operands keep their source order (right is popped first). */
	Operands operands;
	for (auto it = stack_output.begin() + out_head; it != stack_output.end(); ++it) {
		auto& token = *it;
		switch (token.get_type()) {
			case TokenType::Id:
			{
				auto id = std::make_unique<IdNode>(std::move(token.lexeme_ref()));
				const std::string* head = &id->id_ref();
				operands.push({std::move(id), 1, head});
			}
				break;
			case TokenType::Not: {
				operands.need(1);
				auto operand = operands.pop();
				operands.push({std::make_unique<NotNode>(std::move(operand.node)), operand.depth + 1, &token.get_lexeme()});
				break;
			}
			case TokenType::Or:
			case TokenType::And:
			case TokenType::Maybe:
			case TokenType::Xor: {
				operands.need(2);
				auto rig = operands.pop();
				auto lef = operands.pop();
				auto depth = std::max(lef.depth, rig.depth) + 1;
				std::unique_ptr<BaseNode> node;
				switch (token.get_type()) {
					case TokenType::Or:    node = std::make_unique<OrNode>(std::move(lef.node), std::move(rig.node)); break;
					case TokenType::And:   node = std::make_unique<AndNode>(std::move(lef.node), std::move(rig.node)); break;
					case TokenType::Maybe: node = std::make_unique<MaybeNode>(std::move(lef.node), std::move(rig.node)); break;
					default:               node = std::make_unique<XorNode>(std::move(lef.node), std::move(rig.node)); break;
				}
				operands.push({std::move(node), depth, &token.get_lexeme()});
				break;
			}
			default: {
				std::string msj = "'" + token.get_lexeme() + "' not expected";
				throw SyntacticException(msj.c_str());
			}
		}
	}
	operands.need(1);
	if (operands.size() > 1) {
		std::string msj = "'" + *operands.at(operands.size() - 2).head + "' not expected";
		throw SyntacticException(msj.c_str());
	}
	stack_output.clear();
	out_head = 0;
	auto result = operands.pop();
	root_depth = result.depth;
	return std::move(result.node);
}


/*
 * Convert to RPN (Reverse Polish notation)
 * with Dijkstra's Shunting-yard algorithm.
 */
void
BooleanTree::toRPN()
{
	currentToken = lexer.NextToken();
	// True right after an operand ends (an Id or a closing parenthesis): a term, an
	// opening parenthesis or NOT that follows is joined to it by the implicit operator.
	bool operand_ended = false;

	// Pops every operator that binds at least as tightly as `type` (a parenthesis never
	// does), which makes the binary operators left associative.
	auto pop_tighter = [&](TokenType type) {
		while (!stack_operator.empty() && precedence(type) >= precedence(stack_operator.back().get_type())) {
			stack_output.push_back(std::move(stack_operator.back()));
			stack_operator.pop_back();
		}
	};
	// The implicit operator is emitted before the operand that follows it, after popping
	// the operators that bind tighter, so `a & b c` is `(a & b) | c`.
	auto implicit_operator = [&]() {
		if (operand_ended) {
			pop_tighter(DEFAULT_OPERATOR);
			stack_operator.emplace_back(DEFAULT_OPERATOR);
		}
	};

	while (currentToken.get_type() != TokenType::EndOfFile) {
		switch (currentToken.get_type()) {
			case TokenType::Id:
				implicit_operator();
				stack_output.push_back(std::move(currentToken));
				operand_ended = true;
				break;

			case TokenType::LeftParenthesis:
				implicit_operator();
				stack_operator.push_back(std::move(currentToken));
				operand_ended = false;
				break;

			case TokenType::RightParenthesis:
				while (true) {
					if (!stack_operator.empty()) {
						if (stack_operator.back().get_type() != TokenType::LeftParenthesis) {
							stack_output.push_back(std::move(stack_operator.back()));
							stack_operator.pop_back();
						} else {
							stack_operator.pop_back();
							break;
						}
					} else {
						std::string msj = ") was expected";
						throw SyntacticException(msj.c_str());
					}
				}
				operand_ended = true;
				break;

			case TokenType::Not:
				// Prefix operator: it pops nothing, so `NOT NOT a` nests.
				implicit_operator();
				stack_operator.push_back(std::move(currentToken));
				operand_ended = false;
				break;

			case TokenType::Or:
			case TokenType::And:
			case TokenType::Maybe:
			case TokenType::Xor:
				pop_tighter(currentToken.get_type());
				stack_operator.push_back(std::move(currentToken));
				operand_ended = false;
				break;

			case TokenType::EndOfFile:
				break;
		}
		currentToken = lexer.NextToken();
	}

	while (!stack_operator.empty()) {
		stack_output.push_back(std::move(stack_operator.back()));
		stack_operator.pop_back();
	}
}


unsigned
BooleanTree::precedence(TokenType type)
{
	switch (type) {
		case TokenType::Not:
			return 0;
		case TokenType::And:
			return 1;
		case TokenType::Maybe:
			return 2;
		case TokenType::Xor:
			return 3;
		case TokenType::Or:
			return 4;
		default:
			return 5;
	}
}


void
BooleanTree::PrintTree()
{
	postorder(root.get());
}


void
BooleanTree::postorder(BaseNode* p, int indent)
{
	if (p != nullptr) {
		switch (p->getType()) {
			case NodeType::AND:
				if (dynamic_cast<AndNode*>(p)->getLeftNode() != nullptr) {
					postorder(dynamic_cast<AndNode*>(p)->getLeftNode(), indent + 4);
				}
				if (indent != 0) {
					std::cout << std::setw(indent) << ' ';
				}
				std::cout << "AND" << "\n ";
				if (dynamic_cast<AndNode*>(p)->getRightNode() != nullptr) {
					postorder(dynamic_cast<AndNode*>(p)->getRightNode(), indent + 4);
				}
				break;
			case NodeType::MAYBE:
				if (dynamic_cast<MaybeNode*>(p)->getLeftNode() != nullptr) {
					postorder(dynamic_cast<MaybeNode*>(p)->getLeftNode(), indent + 4);
				}
				if (indent != 0) {
					std::cout << std::setw(indent) << ' ';
				}
				std::cout << "MAYBE" << "\n ";
				if (dynamic_cast<AndNode*>(p)->getRightNode() != nullptr) {
					postorder(dynamic_cast<AndNode*>(p)->getRightNode(), indent + 4);
				}
				break;
			case NodeType::OR:
				if (dynamic_cast<OrNode*>(p)->getLeftNode() != nullptr) {
					postorder(dynamic_cast<OrNode*>(p)->getLeftNode(), indent + 4);
				}
				if (indent != 0) {
					std::cout << std::setw(indent) << ' ';
				}
				std::cout << "OR" << "\n ";
				if (dynamic_cast<OrNode*>(p)->getRightNode() != nullptr) {
					postorder(dynamic_cast<OrNode*>(p)->getRightNode(), indent + 4);
				}
				break;
			case NodeType::NOT:
				if (dynamic_cast<NotNode*>(p) != nullptr) {
					std::cout << std::setw(indent) << ' ';
					std::cout << "NOT" << "\n ";
					postorder(dynamic_cast<NotNode*>(p)->getNode(), indent + 4);
				}
				break;
			case NodeType::XOR:
				if (dynamic_cast<XorNode*>(p)->getLeftNode() != nullptr) {
					postorder(dynamic_cast<XorNode*>(p)->getLeftNode(), indent + 4);
				}
				if (indent != 0) {
					std::cout << std::setw(indent) << ' ';
				}
				std::cout << "XOR" << "\n ";
				if (dynamic_cast<XorNode*>(p)->getRightNode() != nullptr) {
					postorder(dynamic_cast<XorNode*>(p)->getRightNode(), indent + 4);
				}
				break;
			case NodeType::ID:
				std::cout << std::setw(indent) << ' ';
				if (dynamic_cast<IdNode*>(p) != nullptr) {
					std::cout << dynamic_cast<IdNode*>(p)->getId() << "\n ";
				}
				break;
		}
	}
}
