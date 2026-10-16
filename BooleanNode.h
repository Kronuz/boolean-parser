/*
 * Copyright (c) 2015-2018 Dubalu LLC
 * Copyright (c) 2014 furan
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

#include <cstdint>
#include <memory>
#include <vector>


enum class NodeType : uint8_t {
	AND,
	MAYBE,
	OR,
	NOT,
	XOR,
	ID,
};


class BaseNode {
public:
	virtual ~BaseNode() = default;

	virtual NodeType getType() const noexcept = 0;

	// Moves this node's children into `out`, so a tree can be torn down without recursion.
	virtual void release_children(std::vector<std::unique_ptr<BaseNode>>& out) { (void)out; }
};


// Destroys a tree iteratively: nesting depth is bounded by memory, not the native stack.
inline void destroy_tree(std::unique_ptr<BaseNode> node) {
	std::vector<std::unique_ptr<BaseNode>> pending;
	if (node) {
		pending.push_back(std::move(node));
	}
	while (!pending.empty()) {
		auto next = std::move(pending.back());
		pending.pop_back();
		next->release_children(pending);
	}
}
