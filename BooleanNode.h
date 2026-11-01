/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
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

	// Nodes up to 64 bytes come from a small per-thread cache of freed blocks (a tree is
	// built and torn down one node at a time, and most of the cost is the allocator). The
	// cache gives any block back to the system allocator past 256 per size, and at thread
	// exit, so it can be freed on any thread. The other forms are the usual ones.
	static void* operator new(std::size_t size);
	static void operator delete(void* block, std::size_t size) noexcept;
	static void* operator new(std::size_t size, std::align_val_t align) { return ::operator new(size, align); }
	static void operator delete(void* block, std::size_t, std::align_val_t align) noexcept { ::operator delete(block, align); }
	static void* operator new(std::size_t, void* where) noexcept { return where; }
	static void operator delete(void*, void*) noexcept { }
	static void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
		try { return operator new(size); } catch (...) { return nullptr; }
	}
	static void operator delete(void* block, const std::nothrow_t&) noexcept { ::operator delete(block); }

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
