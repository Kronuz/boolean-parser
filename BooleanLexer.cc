/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
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

#include "BooleanLexer.h"

#include <cstring>
#include <string>


constexpr const char AND[]    = "AND";
constexpr const char MAYBE[]  = "MAYBE";
constexpr const char OR[]     = "OR";
constexpr const char NOT[]    = "NOT";
constexpr const char XOR[]    = "XOR";


constexpr char DOUBLEQUOTE            = '"';
constexpr char SINGLEQUOTE            = '\'';
constexpr char LEFT_SQUARE_BRACKET    = '[';
constexpr char RIGHT_SQUARE_BRACKET   = ']';


Lexer::Lexer(char* input)
	: cs(input), pos(input), cur('\0'), saw_newline(false)
{
	advance();
}


namespace {

// Bytes that need the general state machine inside a bare token: the operator
// characters, the quotes, whitespace-like bytes that the reader treats specially,
// and the end of the input. Everything else extends the token.
struct StopTable {
	bool stop[256];
	constexpr StopTable() : stop() {
		for (char c : {'(', ')', '&', '|', '!', '"', '\'', ' ', '\0', '\n', '\r'}) {
			stop[static_cast<unsigned char>(c)] = true;
		}
	}
};
constexpr StopTable stop_table;

// The lexeme of the symbols consumed from `start` up to `end`: the bytes in between,
// except that the second byte of a "\n\r" or "\r\n" pair was skipped by the reader.
std::string
make_lexeme(const char* start, const char* end, bool saw_newline)
{
	std::string lexeme(start, end);
	if (saw_newline && lexeme.find_first_of("\r\n") != std::string::npos) {
		lexeme.clear();
		for (const char* p = start; p != end; ++p) {
			lexeme += *p;
			if ((*p == '\n' && p + 1 != end && p[1] == '\r') || (*p == '\r' && p + 1 != end && p[1] == '\n')) {
				++p;
			}
		}
	}
	return lexeme;
}

}  // namespace


Token
Lexer::NextToken()
{
	// The lexeme is the run of symbols from the first non-blank one up to (not
	// including) the current symbol, so it is only a pair of pointers until a token
	// is returned.
	const char* start = nullptr;
	saw_newline = false;
	LexerState currentState = LexerState::INIT;
	Token token;
	char quote = '\0';

	auto upState = currentState;

	while (true) {
		switch (currentState) {
			case LexerState::INIT:
				switch (cur) {
					case LEFT_SQUARE_BRACKET:
						start = cs;
						currentState = LexerState::INIT_SQUARE_BRACKET;
						advance();
						break;

					case SINGLEQUOTE:
						start = cs;
						currentState = LexerState::TOKEN_QUOTE;
						quote = SINGLEQUOTE;
						advance();
						break;

					case DOUBLEQUOTE:
						start = cs;
						currentState = LexerState::TOKEN_QUOTE;
						quote = DOUBLEQUOTE;
						advance();
						break;

					case '\0':
						currentState = LexerState::EOFILE;
						break;

					case ' ':
					case '\n':
					case '\t':
					case '\r':
						advance();
						break;

					case '(':
					case ')':
					case '&':
					case '|':
					case '!':
						start = cs;
						currentState = LexerState::SYMBOL_OP;
						advance();
						break;

					default:
						start = cs;
						currentState = LexerState::TOKEN;
						advance();
						break;
				}
				break;

			case LexerState::TOKEN:
				switch (cur) {
					case DOUBLEQUOTE:
						currentState = LexerState::TOKEN_QUOTE;
						quote = DOUBLEQUOTE;
						advance();
						break;

					case SINGLEQUOTE:
						currentState = LexerState::TOKEN_QUOTE;
						quote = SINGLEQUOTE;
						advance();
						break;

					default:
						if (!IsSymbolOp(cur) && cur != ' ' && cur != '\0') {
							// Skip the run of ordinary bytes that follows in one tight loop.
							const char* q = pos;
							while (!stop_table.stop[static_cast<unsigned char>(*q)]) { ++q; }
							pos = q;
							advance();
						} else {
							token.set_lexeme(make_lexeme(start, cs, saw_newline));
							token.set_type(TokenType::Id);
							IsStringOperator(token);
							return token;
						}
				}
				break;

			case LexerState::TOKEN_QUOTE:
				switch (cur) {
					case '\\':
						currentState = LexerState::ESCAPE;
						advance();
						break;

					case '\0':
						throw LexicalException("Symbol double quote expected");

					default:
						if (cur == quote) {
							upState == LexerState::INIT_SQUARE_BRACKET ? currentState = LexerState::END_SQUARE_BRACKET : currentState = LexerState::TOKEN;
						} else {
							// Skip the rest of the plain bytes of this quoted run.
							const char* q = pos;
							while (*q != quote && *q != '\\' && *q != '\0' && *q != '\n' && *q != '\r') { ++q; }
							pos = q;
						}
						advance();
				}
				break;

			case LexerState::ESCAPE:
				switch (cur) {
					case '\0':
						throw LexicalException("Symbol EOF not expected");

					default:
						currentState = LexerState::TOKEN_QUOTE;
						advance();
						break;
				}
				break;

			case LexerState::INIT_SQUARE_BRACKET:
				switch (cur) {
					case DOUBLEQUOTE:
						currentState = LexerState::TOKEN_QUOTE;
						upState = LexerState::INIT_SQUARE_BRACKET;
						quote = DOUBLEQUOTE;
						advance();
						continue;

					case SINGLEQUOTE:
						currentState = LexerState::TOKEN_QUOTE;
						upState = LexerState::INIT_SQUARE_BRACKET;
						quote = SINGLEQUOTE;
						advance();
						continue;

					default:
						if (cur != RIGHT_SQUARE_BRACKET && cur != '\0') {
							advance();
							continue;
						}
				}
				[[fallthrough]];

			case LexerState::END_SQUARE_BRACKET:
				switch (cur) {
					case RIGHT_SQUARE_BRACKET:
						currentState = LexerState::TOKEN;
						advance();
						break;

					case ',':
						currentState = LexerState::INIT_SQUARE_BRACKET;
						advance();
						break;

					default:
						throw LexicalException("Symbol ] expected");
				}
				break;

			case LexerState::SYMBOL_OP:
				token.set_lexeme(std::string(1, *start));
				switch (*start) {
					case '(':
						token.set_type(TokenType::LeftParenthesis);
						break;
					case ')':
						token.set_type(TokenType::RightParenthesis);
						break;
					case '&':
						token.set_type(TokenType::And);
						break;
					case '|':
						token.set_type(TokenType::Or);
						break;
					case '!':
						token.set_type(TokenType::Not);
						break;
				}
				return token;
			case LexerState::EOFILE:
				token.set_type(TokenType::EndOfFile);
				return token;
			default:
				break;
		}
	}

	return token;
}


void
Lexer::IsStringOperator(Token& token) const
{
	// Case-insensitive match of the five keywords; the length and first letter reject
	// almost every ordinary term before any comparison.
	const auto& lexeme = token.get_lexeme();
	const char* s = lexeme.data();
	auto is = [&](const char* kw, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			if ((s[i] | 0x20) != kw[i]) { return false; }
		}
		return true;
	};
	switch (lexeme.size()) {
		case 2:
			if (is("or", 2)) { token.set_type(TokenType::Or); }
			break;
		case 3:
			if (is("and", 3)) { token.set_type(TokenType::And); }
			else if (is("not", 3)) { token.set_type(TokenType::Not); }
			else if (is("xor", 3)) { token.set_type(TokenType::Xor); }
			break;
		case 5:
			if (is("maybe", 5)) { token.set_type(TokenType::Maybe); }
			break;
		default:
			break;
	}
}


bool
Lexer::IsSymbolOp(char c) const
{
	switch (c) {
		case '(':
		case ')':
		case '&':
		case '|':
		case '!':
			return true;
		default:
			return false;
	}
}
