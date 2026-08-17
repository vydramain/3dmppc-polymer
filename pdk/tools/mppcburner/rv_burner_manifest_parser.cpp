#include "rv_burner_manifest_parser.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "rv_burner_character.hpp"
#include "rv_burner_manifest_assigner.hpp"
#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

std::expected<rv_pdktools::rv_manifest, std::string> rv_pdktools::rv_burner_manifest_parser::run()
{
	for (;;) {
		skip_gaps();
		if (eof()) {
			break;
		}
		const char c = peek();
		if (c == '[') {
			if (!parse_section()) {
				break;
			}
		} else if (rv_pdktools::rv_burner_is_ident(c)) {
			if (!parse_assignment()) {
				break;
			}
		} else {
			rv_burner_manifest_failer::fail(line_,
				std::string("expected a section header or 'key = value', found '") + c + "'", error_);
			break;
		}
	}
	if (!error_.empty()) {
		return std::unexpected(error_);
	}
	return assigner_.take_manifest();
}

bool rv_pdktools::rv_burner_manifest_parser::eof() const
{
	return pos_ >= text_.size();
}
char rv_pdktools::rv_burner_manifest_parser::peek() const
{
	return pos_ < text_.size() ? text_[pos_] : '\0';
}

char rv_pdktools::rv_burner_manifest_parser::get()
{
	const char c = text_[pos_++];
	if (c == '\n') {
		++line_;
	}
	return c;
}

// Spaces, tabs and stray carriage returns — never a newline.
void rv_pdktools::rv_burner_manifest_parser::skip_inline()
{
	while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
		get();
	}
}

void rv_pdktools::rv_burner_manifest_parser::skip_comment()
{
	while (!eof() && peek() != '\n') {
		get();
	}
}

// Everything that may separate two meaningful tokens: whitespace, blank
// lines and full-line comments. Also used INSIDE an array, which is what
// makes a multi-line array work without a special case.
void rv_pdktools::rv_burner_manifest_parser::skip_gaps()
{
	for (;;) {
		skip_inline();
		if (!eof() && peek() == '#') {
			skip_comment();
			continue;
		}
		if (!eof() && peek() == '\n') {
			get();
			continue;
		}
		return;
	}
}

// --- grammar --------------------------------------------------------------

bool rv_pdktools::rv_burner_manifest_parser::parse_section()
{
	const int line = line_;
	get(); // '['
	skip_inline();
	std::string name;
	while (!eof() && rv_pdktools::rv_burner_is_ident(peek())) {
		name += get();
	}
	skip_inline();
	if (name.empty()) {
		return rv_burner_manifest_failer::fail(line, "empty section header", error_);
	}
	if (eof() || peek() != ']') {
		return rv_burner_manifest_failer::fail(line, "section header '[" + name + "' is missing its closing ']'", error_);
	}
	get(); // ']'
	if (rv_burner_sections_get(name) == nullptr) {
		return rv_burner_manifest_failer::fail(line, "unknown section '[" + name + "]'" + suggest_section(name, rv_burner_sections, std::size(rv_burner_sections)), error_);
	}
	if (!seen_sections_.insert(name).second) {
		return rv_burner_manifest_failer::fail(line, "section '[" + name + "]' appears twice", error_);
	}
	section_ = name;
	return expect_line_end("section header");
}

bool rv_pdktools::rv_burner_manifest_parser::parse_assignment()
{
	const int line = line_;
	std::string key;
	while (!eof() && rv_pdktools::rv_burner_is_ident(peek())) {
		key += get();
	}
	skip_inline();
	if (eof() || peek() != '=') {
		return rv_burner_manifest_failer::fail(line, "expected '=' after key '" + key + "'", error_);
	}
	get(); // '='
	skip_inline();
	if (section_.empty()) {
		return rv_burner_manifest_failer::fail(line, "key '" + key + "' appears before any section header", error_);
	}
	rv_pdktools::rv_burner_mvalue value;
	if (!parse_value(value)) {
		return false;
	}
	if (!assigner_.assign(key, value, line)) {
		return false;
	}
	return expect_line_end("value");
}

bool rv_pdktools::rv_burner_manifest_parser::parse_value(rv_burner_mvalue &out)
{
	out.line = line_;
	const char c = peek();
	if (c == '"') {
		out.kind = rv_burner_value_kind::string;
		return parse_string(out.str);
	}
	if (c == '[') {
		out.kind = rv_burner_value_kind::array;
		return parse_array(out.arr);
	}
	if (c == '-' || c == '+' || rv_pdktools::rv_burner_is_digit(c)) {
		out.kind = rv_burner_value_kind::integer;
		return parse_integer(out.num);
	}
	if (eof() || c == '\n' || c == '#') {
		return rv_burner_manifest_failer::fail(out.line, "missing value after '='", error_);
	}
	if (c == '\'') {
		return rv_burner_manifest_failer::fail(out.line, "single-quoted strings are not supported — use double quotes", error_);
	}

	std::string word;
	while (!eof() && peek() != '\n' && peek() != '#' && peek() != ' ' && peek() != '\t' &&
		peek() != '\r') {
		word += get();
	}
	if (word == "true" || word == "false") {
		return rv_burner_manifest_failer::fail(out.line,
			"booleans are not supported — this manifest holds strings, "
			"integers and arrays of strings only",
			error_);
	}
	return rv_burner_manifest_failer::fail(out.line, "unsupported value '" + word + "' — expected a quoted string, an integer or an array of "
																					"quoted strings",
		error_);
}

bool rv_pdktools::rv_burner_manifest_parser::parse_string(std::string &out)
{
	const int start = line_;
	get(); // opening quote
	for (;;) {
		if (eof()) {
			return rv_burner_manifest_failer::fail(start, "unterminated string — no closing '\"' before end of file", error_);
		}
		if (peek() == '\n') {
			return rv_burner_manifest_failer::fail(start, "unterminated string — no closing '\"' before end of line", error_);
		}
		const char c = get();
		if (c == '"') {
			return true;
		}
		if (c != '\\') {
			out += c;
			continue;
		}
		if (eof() || peek() == '\n') {
			return rv_burner_manifest_failer::fail(start, "unterminated string — '\\' at end of line", error_);
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
			return rv_burner_manifest_failer::fail(escape_line, std::string("unknown escape '\\") + e + "' in string — only \\\" \\\\ \\n \\t \\r are "
																										"recognised",
				error_);
		}
	}
}

bool rv_pdktools::rv_burner_manifest_parser::parse_array(std::vector<std::string> &out)
{
	const int start = line_;
	get(); // '['
	for (;;) {
		skip_gaps();
		if (eof()) {
			return rv_burner_manifest_failer::fail(start, "unterminated array — no closing ']' before end of file", error_);
		}
		if (peek() == ']') {
			get();
			return true;
		}
		if (peek() != '"') {
			if (peek() == ',') {
				return rv_burner_manifest_failer::fail(line_, "empty element in array — expected a quoted string", error_);
			}
			return rv_burner_manifest_failer::fail(line_, "array elements must be quoted strings", error_);
		}
		std::string element;
		if (!parse_string(element)) {
			return false;
		}
		out.push_back(std::move(element));

		skip_gaps();
		if (eof()) {
			return rv_burner_manifest_failer::fail(start, "unterminated array — no closing ']' before end of file", error_);
		}
		if (peek() == ',') {
			get();
			continue;
		}
		if (peek() == ']') {
			get();
			return true;
		}
		return rv_burner_manifest_failer::fail(line_, std::string("expected ',' or ']' in array, found '") + peek() + "'", error_);
	}
}

bool rv_pdktools::rv_burner_manifest_parser::parse_integer(int64_t &out)
{
	const int start = line_;
	const std::size_t begin = pos_;
	if (peek() == '-' || peek() == '+') {
		get();
	}
	if (eof() || !rv_pdktools::rv_burner_is_digit(peek())) {
		return rv_burner_manifest_failer::fail(start, "expected a decimal integer", error_);
	}
	while (!eof() && rv_pdktools::rv_burner_is_digit(peek())) {
		get();
	}
	// A trailing word means something like `256px` or `0x20` — a value this
	// dialect does not have, and one that must not silently become 256 or 0.
	if (!eof() && rv_pdktools::rv_burner_is_ident(peek())) {
		std::string word = text_.substr(begin, pos_ - begin);
		while (!eof() && rv_pdktools::rv_burner_is_ident(peek())) {
			word += get();
		}
		return rv_burner_manifest_failer::fail(start, "'" + word + "' is not a decimal integer", error_);
	}

	const std::string digits = text_.substr(begin, pos_ - begin);
	errno = 0;
	char *end = nullptr;
	const long long parsed = std::strtoll(digits.c_str(), &end, 10);
	if (errno == ERANGE || end == digits.c_str()) {
		return rv_burner_manifest_failer::fail(start, "integer '" + digits + "' does not fit in 64 bits", error_);
	}
	out = static_cast<int64_t>(parsed);
	return true;
}

bool rv_pdktools::rv_burner_manifest_parser::expect_line_end(const char *what)
{
	skip_inline();
	if (!eof() && peek() == '#') {
		skip_comment();
	}
	if (eof()) {
		return true;
	}
	if (peek() == '\n') {
		get();
		return true;
	}
	return rv_burner_manifest_failer::fail(line_,
		std::string("unexpected text after ") + what + " — one " + what + " per line", error_);
}
