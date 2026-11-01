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

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include "BooleanToken.h"


// A vector of tokens whose first N elements live inside the object, so the RPN queue and
// the operator stack of an ordinary query allocate nothing. A longer one moves all its
// tokens into a `std::vector`, which then grows the way it always does.
// The object is not copyable or movable: the elements may point into it.
template <std::size_t N>
class TokenVector {
	alignas(Token) unsigned char raw[N * sizeof(Token)];
	std::vector<Token> heap_;   // holds the tokens once there are more than N
	Token* first_;              // the inline storage, or heap_.data()
	Token* last_;

	Token* local() noexcept { return reinterpret_cast<Token*>(raw); }
	bool inline_mode() const noexcept { return first_ == reinterpret_cast<const Token*>(raw); }
	void sync() noexcept {
		first_ = heap_.data();
		last_ = first_ + heap_.size();
	}

	void spill() {
		heap_.reserve(N * 2);
		for (Token* token = first_; token != last_; ++token) {
			heap_.push_back(std::move(*token));
			token->~Token();
		}
		sync();
	}

public:
	TokenVector() noexcept : first_(local()), last_(first_) { }
	TokenVector(const TokenVector&) = delete;
	TokenVector& operator=(const TokenVector&) = delete;

	~TokenVector() {
		if (inline_mode()) {
			clear();
		}
	}

	bool empty() const noexcept { return first_ == last_; }
	std::size_t size() const noexcept { return static_cast<std::size_t>(last_ - first_); }

	Token* begin() noexcept { return first_; }
	Token* end() noexcept { return last_; }
	const Token* begin() const noexcept { return first_; }
	const Token* end() const noexcept { return last_; }

	Token& at(std::size_t i) {
		if (i >= size()) {
			throw std::out_of_range("TokenVector::at");
		}
		return first_[i];
	}
	const Token& at(std::size_t i) const {
		if (i >= size()) {
			throw std::out_of_range("TokenVector::at");
		}
		return first_[i];
	}

	Token& back() noexcept { return last_[-1]; }
	const Token& back() const noexcept { return last_[-1]; }

	void push_back(Token&& token) {
		if (inline_mode()) {
			if (last_ != local() + N) {
				new (last_) Token(std::move(token));
				++last_;
				return;
			}
			spill();
		}
		heap_.push_back(std::move(token));
		sync();
	}

	void emplace_back(TokenType type) {
		if (inline_mode()) {
			if (last_ != local() + N) {
				new (last_) Token(type);
				++last_;
				return;
			}
			spill();
		}
		heap_.emplace_back(type);
		sync();
	}

	void pop_back() noexcept {
		if (inline_mode()) {
			--last_;
			last_->~Token();
		} else {
			heap_.pop_back();
			sync();
		}
	}

	void clear() noexcept {
		if (inline_mode()) {
			while (last_ != first_) {
				pop_back();
			}
		} else {
			heap_.clear();
			sync();
		}
	}
};
