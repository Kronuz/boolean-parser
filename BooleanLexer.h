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

#include "ContentReader.h"
#include "LexicalException.h"
#include "BooleanToken.h"


enum class LexerState : uint8_t {
	INIT,
	TOKEN,
	TOKEN_QUOTE,
	INIT_SQUARE_BRACKET,
	END_SQUARE_BRACKET,
	ESCAPE,
	SYMBOL_OP,
	EOFILE
};


// Lexes a NUL-terminated buffer that must outlive the Lexer. The scanner works on
// pointers into that buffer and builds each lexeme with one allocation (none up to
// the small-string size). It reproduces the ContentReader convention that a "\n\r"
// or "\r\n" pair is a single symbol, so the second byte of a pair never reaches a
// lexeme.
class Lexer {
	const char* cs;       // start of the current (peeked) symbol
	const char* pos;      // just after it, including a skipped "\n\r" / "\r\n" partner
	char cur;             // the current symbol; '\0' at the end of the input
	bool saw_newline;     // a "\n" or "\r" was read since NextToken began (lexeme needs the pair rule)

	void advance() noexcept {
		cs = pos;
		cur = *pos;
		if (cur != '\0') {
			++pos;
			if (cur == '\n' || cur == '\r') {
				saw_newline = true;
				if (*pos == (cur == '\n' ? '\r' : '\n')) {
					++pos;
				}
			}
		}
	}

	void IsStringOperator(Token& token) const;
	bool IsSymbolOp(char c) const;

public:
	explicit Lexer(char* input);
	Token NextToken();
};
