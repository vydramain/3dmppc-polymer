#include "rv_burner_manifest_lexer.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "rv_burner_character.hpp"

namespace rv_pdktools
{
using tk = rv_burner_token_kind;

static rv_burner_token token_of(tk kind, int line, std::string text = std::string())
{
	rv_burner_token token;
	token.kind = kind;
	token.line = line;
	token.text = std::move(text);
	return token;
}

static rv_burner_token invalid(int line, std::string message)
{
	return token_of(tk::INVALID, line, std::move(message));
}
bool rv_burner_manifest_lexer::eof() const
{
	return pos_ >= text_.size();
}

char rv_burner_manifest_lexer::peek(std::size_t ahead) const
{
	return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : '\0';
}

char rv_burner_manifest_lexer::get()
{
	const char c = text_[pos_++];
	if (c == '\n') {
		++line_;
	}
	return c;
}

void rv_burner_manifest_lexer::skip_inline()
{
	while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
		get();
	}
}

void rv_burner_manifest_lexer::skip_comment()
{
	while (!eof() && peek() != '\n') {
		get();
	}
}

rv_burner_token rv_burner_manifest_lexer::next()
{
	for (;;) {
		skip_inline();
		if (eof()) {
			return token_of(tk::END_OF_FILE, line_);
		}
		const char c = peek();
		if (c == '#') {
			skip_comment();
			continue;
		}
		// The line is read before get(), which is what advances the counter.
		const int line = line_;
		switch (c) {
		case '\n':
			get();
			return token_of(tk::NEWLINE, line);
		case '=':
			get();
			return token_of(tk::EQUALS, line);
		case '[':
			get();
			return token_of(tk::LBRACKET, line);
		case ']':
			get();
			return token_of(tk::RBRACKET, line);
		case ',':
			get();
			return token_of(tk::COMMA, line);
		case '"':
			return lex_string();
		case '\'':
			get();
			return invalid(line, "single-quoted strings are not supported — use double quotes");
		default:
			break;
		}

		// A sign only starts a number when a digit follows; otherwise '-' is an
		// identifier character like any other.
		if (rv_burner_is_digit(c) || ((c == '-' || c == '+') && rv_burner_is_digit(peek(1)))) {
			return lex_integer();
		}
		if (rv_burner_is_ident(c)) {
			return lex_ident();
		}
		get();
		return invalid(line, std::string("unexpected character '") + c + "'");
	}
}

rv_burner_token rv_burner_manifest_lexer::lex_ident()
{
	const int line = line_;
	std::string word;
	while (!eof() && rv_burner_is_ident(peek())) {
		word += get();
	}
	return token_of(tk::IDENT, line, std::move(word));
}

// Escapes are decoded here, so the parser and everything after it sees the text
// the author meant. Only the five the renderer emits are recognised.
rv_burner_token rv_burner_manifest_lexer::lex_string()
{
	const int line = line_;
	get(); // opening quote
	std::string out;
	for (;;) {
		if (eof()) {
			return invalid(line, "unterminated string — no closing '\"' before end of file");
		}
		if (peek() == '\n') {
			return invalid(line, "unterminated string — no closing '\"' before end of line");
		}
		const char c = get();
		if (c == '"') {
			return token_of(tk::STRING, line, std::move(out));
		}
		if (c != '\\') {
			out += c;
			continue;
		}
		if (eof() || peek() == '\n') {
			return invalid(line, "unterminated string — '\\' at end of line");
		}
		const int escape_line = line_;
		const char e = get();
		switch (e) {
		case '"':
			out += '"';
			break;
		case '\\':
			out += '\\';
			break;
		case 'n':
			out += '\n';
			break;
		case 't':
			out += '\t';
			break;
		case 'r':
			out += '\r';
			break;
		default:
			return invalid(escape_line,
				std::string("unknown escape '\\") + e +
					"' in string — only \\\" \\\\ \\n \\t \\r are recognised");
		}
	}
}

rv_burner_token rv_burner_manifest_lexer::lex_integer()
{
	const int line = line_;
	const std::size_t begin = pos_;
	if (peek() == '-' || peek() == '+') {
		get();
	}
	while (!eof() && rv_burner_is_digit(peek())) {
		get();
	}
	// A trailing word means something like `256px` or `0x20` — a value this
	// dialect does not have, and one that must not silently become 256 or 0.
	if (!eof() && rv_burner_is_ident(peek())) {
		while (!eof() && rv_burner_is_ident(peek())) {
			get();
		}
		return invalid(line, "'" + text_.substr(begin, pos_ - begin) + "' is not a decimal integer");
	}

	const std::string digits = text_.substr(begin, pos_ - begin);
	errno = 0;
	char *end = nullptr;
	const long long parsed = std::strtoll(digits.c_str(), &end, 10);
	if (errno == ERANGE) {
		return invalid(line, "integer '" + digits + "' does not fit in 64 bits");
	}
	rv_burner_token token = token_of(tk::INTEGER, line);
	token.num = static_cast<int64_t>(parsed);
	return token;
}

std::vector<rv_burner_token> rv_burner_manifest_lex(const std::string &text)
{
	rv_burner_manifest_lexer lexer(text);
	std::vector<rv_burner_token> tokens;
	for (;;) {
		rv_burner_token token = lexer.next();
		const bool last = token.kind == tk::END_OF_FILE;
		tokens.push_back(std::move(token));
		if (last) {
			return tokens;
		}
	}
}

} // namespace rv_pdktools
