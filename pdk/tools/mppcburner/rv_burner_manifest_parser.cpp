#include "rv_burner_manifest_parser.hpp"

#include "rv_burner_character.hpp"
#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

bool rv_pdktools::rv_burner_manifest_parser::run(std::string &error)
{
	manifest_ = rv_manifest{};
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
			fail(line_,
				std::string("expected a section header or 'key = value', found '") + c + "'");
			break;
		}
	}
	if (!error_.empty()) {
		error = error_;
		return false;
	}
	return true;
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

bool rv_pdktools::rv_burner_manifest_parser::fail(int line, const std::string &message)
{
	if (error_.empty()) {
		error_ = "line " + std::to_string(line) + ": " + message;
	}
	return false;
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
		return fail(line, "empty section header");
	}
	if (eof() || peek() != ']') {
		return fail(line, "section header '[" + name + "' is missing its closing ']'");
	}
	get(); // ']'
	if (rv_burner_sections_get(name) == nullptr) {
		return fail(line, "unknown section '[" + name + "]'" + suggest_section(name, rv_burner_sections, std::size(rv_burner_sections)));
	}
	if (!seen_sections_.insert(name).second) {
		return fail(line, "section '[" + name + "]' appears twice");
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
		return fail(line, "expected '=' after key '" + key + "'");
	}
	get(); // '='
	skip_inline();
	if (section_.empty()) {
		return fail(line, "key '" + key + "' appears before any section header");
	}
	rv_pdktools::rv_burner_mvalue value;
	if (!parse_value(value)) {
		return false;
	}
	if (!assign(key, value, line)) {
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
		return fail(out.line, "missing value after '='");
	}
	if (c == '\'') {
		return fail(out.line, "single-quoted strings are not supported — use double quotes");
	}

	std::string word;
	while (!eof() && peek() != '\n' && peek() != '#' && peek() != ' ' && peek() != '\t' &&
		peek() != '\r') {
		word += get();
	}
	if (word == "true" || word == "false") {
		return fail(out.line,
			"booleans are not supported — this manifest holds strings, "
			"integers and arrays of strings only");
	}
	return fail(out.line, "unsupported value '" + word + "' — expected a quoted string, an integer or an array of "
														 "quoted strings");
}

bool rv_pdktools::rv_burner_manifest_parser::parse_string(std::string &out)
{
	const int start = line_;
	get(); // opening quote
	for (;;) {
		if (eof()) {
			return fail(start, "unterminated string — no closing '\"' before end of file");
		}
		if (peek() == '\n') {
			return fail(start, "unterminated string — no closing '\"' before end of line");
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
			return fail(start, "unterminated string — '\\' at end of line");
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
			return fail(escape_line, std::string("unknown escape '\\") + e + "' in string — only \\\" \\\\ \\n \\t \\r are "
																			 "recognised");
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
			return fail(start, "unterminated array — no closing ']' before end of file");
		}
		if (peek() == ']') {
			get();
			return true;
		}
		if (peek() != '"') {
			if (peek() == ',') {
				return fail(line_, "empty element in array — expected a quoted string");
			}
			return fail(line_, "array elements must be quoted strings");
		}
		std::string element;
		if (!parse_string(element)) {
			return false;
		}
		out.push_back(std::move(element));

		skip_gaps();
		if (eof()) {
			return fail(start, "unterminated array — no closing ']' before end of file");
		}
		if (peek() == ',') {
			get();
			continue;
		}
		if (peek() == ']') {
			get();
			return true;
		}
		return fail(line_, std::string("expected ',' or ']' in array, found '") + peek() + "'");
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
		return fail(start, "expected a decimal integer");
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
		return fail(start, "'" + word + "' is not a decimal integer");
	}

	const std::string digits = text_.substr(begin, pos_ - begin);
	errno = 0;
	char *end = nullptr;
	const long long parsed = std::strtoll(digits.c_str(), &end, 10);
	if (errno == ERANGE || end == digits.c_str()) {
		return fail(start, "integer '" + digits + "' does not fit in 64 bits");
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
	return fail(line_,
		std::string("unexpected text after ") + what + " — one " + what + " per line");
}

// --- binding a value to a field -------------------------------------------

bool rv_pdktools::rv_burner_manifest_parser::wrong_type(const std::string &key, const rv_burner_mvalue &v, rv_burner_value_kind want)
{
	return fail(v.line, "key '" + key + "' in [" + section_ + "] takes " + std::string(rv_burner_kind_name(want)) + ", not " + std::string(rv_burner_kind_name(v.kind)));
}

bool rv_pdktools::rv_burner_manifest_parser::assign(const std::string &key, const rv_burner_mvalue &value, int key_line)
{
	const section_spec *spec = rv_burner_sections_get(section_);
	if (spec == nullptr) {
		return fail(key_line, "internal error: unknown current section");
	}

	bool known = false;
	for (std::size_t i = 0; i < spec->key_count; ++i) {
		if (spec->keys[i] == key) {
			known = true;
		}
	}
	if (!known) {
		return fail(key_line, "unknown key '" + key + "' in [" + section_ + "]" + suggest(key, spec->keys, spec->key_count));
	}
	if (!seen_keys_.insert(section_ + "." + key).second) {
		return fail(key_line, "key '" + key + "' in [" + section_ + "] is set twice");
	}

	if (section_ == "disc") {
		if (key == "id" || key == "title") {
			if (value.kind != rv_burner_value_kind::string) {
				return wrong_type(key, value, rv_burner_value_kind::string);
			}
			(key == "id" ? manifest_.disc_id : manifest_.disc_title) = value.str;
			return true;
		}
		if (value.kind != rv_burner_value_kind::integer) {
			return wrong_type(key, value, rv_burner_value_kind::integer);
		}
		return true;
	}

	if (section_ == "build" || section_ == "assets" ||
		(section_ == "textures" && key == "files")) {
		if (value.kind != rv_burner_value_kind::array) {
			return wrong_type(key, value, rv_burner_value_kind::array);
		}
		if (key == "sources") {
			manifest_.build_sources = value.arr;
		} else if (key == "defines") {
			manifest_.build_defines = value.arr;
		} else if (key == "include_dirs") {
			manifest_.build_include_dirs = value.arr;
		} else if (section_ == "assets") {
			manifest_.assets_files = value.arr;
		} else {
			manifest_.textures_files.files = value.arr;
		}
		return true;
	}

	if (section_ == "textures") { // key == "format"
		if (value.kind != rv_burner_value_kind::string) {
			return wrong_type(key, value, rv_burner_value_kind::string);
		}
		manifest_.textures_files.format = value.str;
		return true;
	}

	// [budget]
	if (value.kind != rv_burner_value_kind::integer) {
		return wrong_type(key, value, rv_burner_value_kind::integer);
	}
	if (key == "texture_max_width") {
		manifest_.budget.texture_max_width = value.num;
	} else if (key == "texture_max_height") {
		manifest_.budget.texture_max_height = value.num;
	} else {
		manifest_.budget.video_memory_size = value.num;
	}
	return true;
}
