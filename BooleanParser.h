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

#pragma once

#include <memory>
#include <string_view>      // for std::string_view
#include <stdexcept>
#include <vector>

#include "ContentReader.h"
#include "BooleanLexer.h"
#include "BooleanNode.h"


class BooleanTree {
public:
	explicit BooleanTree(std::string_view input_);
	~BooleanTree() { release_root(); }
	BooleanTree(const BooleanTree&) = delete;
	BooleanTree& operator=(const BooleanTree&) = delete;

	std::unique_ptr<BaseNode> root;

	void Parse();
	void postorder(BaseNode* p, int indent=0);
	void PrintTree();

	inline bool empty() const noexcept {
		return stack_output.size() == out_head;
	}

	inline size_t size() const noexcept {
		return stack_output.size() - out_head;
	}

	inline const Token& front() const {
		return stack_output.at(out_head);
	}

	inline Token& front() {
		return stack_output.at(out_head);
	}

	inline const Token& back() const {
		return stack_output.back();
	}

	inline Token& back() {
		return stack_output.back();
	}

	inline void pop_front() {
		// The queue is a vector with a moving head: popping the front releases the token
		// and advances the head, so it stays O(1) and one allocation.
		stack_output.at(out_head) = Token();
		if (++out_head == stack_output.size()) {
			stack_output.clear();
			out_head = 0;
		}
	}

	inline void pop_back() {
		if (stack_output.size() == out_head) {
			throw std::out_of_range("BooleanTree::pop_back on an empty queue");
		}
		stack_output.pop_back();
	}


private:
	void toRPN();
	std::unique_ptr<BaseNode> BuildTree();
	void release_root();
	unsigned precedence(TokenType type);

	std::vector<Token> stack_output;   // the RPN queue: [out_head, size)
	std::size_t out_head = 0;
	std::size_t root_depth = 0;
	std::vector<Token> stack_operator;

	std::unique_ptr<char[]> input;
	Lexer lexer;
	Token currentToken;
};
