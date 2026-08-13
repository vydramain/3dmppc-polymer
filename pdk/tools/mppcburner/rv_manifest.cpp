#include "rv_manifest.hpp"

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

namespace rv_pdktools
{
namespace
{

const section_spec *find_section(std::string_view name)
{
	for (const section_spec &s : rv_burn_sections) {
		if (s.name == name) {
			return &s;
		}
	}
	return nullptr;
}

// --- character classes --------------------------------------------------------

bool is_ident(char c)
{
	const unsigned char u = static_cast<unsigned char>(c);
	return std::isalnum(u) != 0 || c == '_' || c == '-';
}

bool is_digit(char c)
{
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

// --- the parsed value ---------------------------------------------------------

enum class value_kind {
	string,
	integer,
	array
};

struct mvalue {
	value_kind kind = value_kind::string;
	std::string str;
	int64_t num = 0;
	std::vector<std::string> arr;
	int line = 0; // where the value STARTED, which is where the author must look
};

std::string_view kind_name(value_kind k)
{
	switch (k) {
	case value_kind::string:
		return "a string";
	case value_kind::integer:
		return "an integer";
	case value_kind::array:
		return "an array of strings";
	}
	return "a value";
}

// --- the parser ---------------------------------------------------------------

class manifest_parser
{
public:
	manifest_parser(const std::string &text, rv_manifest &out)
		: text_(text)
		, manifest_(out)
	{
	}

	bool run(std::string &error)
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
			} else if (is_ident(c)) {
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

private:
	// --- cursor ---------------------------------------------------------------

	bool eof() const
	{
		return pos_ >= text_.size();
	}
	char peek() const
	{
		return pos_ < text_.size() ? text_[pos_] : '\0';
	}

	char get()
	{
		const char c = text_[pos_++];
		if (c == '\n') {
			++line_;
		}
		return c;
	}

	// Spaces, tabs and stray carriage returns — never a newline.
	void skip_inline()
	{
		while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
			get();
		}
	}

	void skip_comment()
	{
		while (!eof() && peek() != '\n') {
			get();
		}
	}

	// Everything that may separate two meaningful tokens: whitespace, blank
	// lines and full-line comments. Also used INSIDE an array, which is what
	// makes a multi-line array work without a special case.
	void skip_gaps()
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

	bool fail(int line, const std::string &message)
	{
		if (error_.empty()) {
			error_ = "line " + std::to_string(line) + ": " + message;
		}
		return false;
	}

	// --- grammar --------------------------------------------------------------

	bool parse_section()
	{
		const int line = line_;
		get(); // '['
		skip_inline();
		std::string name;
		while (!eof() && is_ident(peek())) {
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
		if (find_section(name) == nullptr) {
			return fail(line, "unknown section '[" + name + "]'" + suggest_section(name, rv_burn_sections, std::size(rv_burn_sections)));
		}
		if (!seen_sections_.insert(name).second) {
			return fail(line, "section '[" + name + "]' appears twice");
		}
		section_ = name;
		return expect_line_end("section header");
	}

	bool parse_assignment()
	{
		const int line = line_;
		std::string key;
		while (!eof() && is_ident(peek())) {
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
		mvalue value;
		if (!parse_value(value)) {
			return false;
		}
		if (!assign(key, value, line)) {
			return false;
		}
		return expect_line_end("value");
	}

	bool parse_value(mvalue &out)
	{
		out.line = line_;
		const char c = peek();
		if (c == '"') {
			out.kind = value_kind::string;
			return parse_string(out.str);
		}
		if (c == '[') {
			out.kind = value_kind::array;
			return parse_array(out.arr);
		}
		if (c == '-' || c == '+' || is_digit(c)) {
			out.kind = value_kind::integer;
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

	bool parse_string(std::string &out)
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

	bool parse_array(std::vector<std::string> &out)
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

	bool parse_integer(int64_t &out)
	{
		const int start = line_;
		const std::size_t begin = pos_;
		if (peek() == '-' || peek() == '+') {
			get();
		}
		if (eof() || !is_digit(peek())) {
			return fail(start, "expected a decimal integer");
		}
		while (!eof() && is_digit(peek())) {
			get();
		}
		// A trailing word means something like `256px` or `0x20` — a value this
		// dialect does not have, and one that must not silently become 256 or 0.
		if (!eof() && is_ident(peek())) {
			std::string word = text_.substr(begin, pos_ - begin);
			while (!eof() && is_ident(peek())) {
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

	bool expect_line_end(const char *what)
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

	bool wrong_type(const std::string &key, const mvalue &v, value_kind want)
	{
		return fail(v.line, "key '" + key + "' in [" + section_ + "] takes " + std::string(kind_name(want)) + ", not " + std::string(kind_name(v.kind)));
	}

	bool assign(const std::string &key, const mvalue &value, int key_line)
	{
		const section_spec *spec = find_section(section_);
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
				if (value.kind != value_kind::string) {
					return wrong_type(key, value, value_kind::string);
				}
				(key == "id" ? manifest_.id : manifest_.title) = value.str;
				return true;
			}
			if (value.kind != value_kind::integer) {
				return wrong_type(key, value, value_kind::integer);
			}
			return true;
		}

		if (section_ == "build" || section_ == "assets" ||
			(section_ == "textures" && key == "files")) {
			if (value.kind != value_kind::array) {
				return wrong_type(key, value, value_kind::array);
			}
			if (key == "sources") {
				manifest_.sources = value.arr;
			} else if (key == "defines") {
				manifest_.defines = value.arr;
			} else if (key == "include_dirs") {
				manifest_.include_dirs = value.arr;
			} else if (section_ == "assets") {
				manifest_.files = value.arr;
			} else {
				manifest_.textures.files = value.arr;
			}
			return true;
		}

		if (section_ == "textures") { // key == "format"
			if (value.kind != value_kind::string) {
				return wrong_type(key, value, value_kind::string);
			}
			manifest_.textures.format = value.str;
			return true;
		}

		// [budget]
		if (value.kind != value_kind::integer) {
			return wrong_type(key, value, value_kind::integer);
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

	const std::string &text_;
	rv_manifest &manifest_;
	std::size_t pos_ = 0;
	int line_ = 1;
	std::string section_;
	std::set<std::string> seen_sections_;
	std::set<std::string> seen_keys_;
	std::string error_;
};

// --- rendering ----------------------------------------------------------------

// The inverse of parse_string(). Only the five escapes the parser knows are
// ever emitted, so anything this function produces is something it can read
// back — that is the whole round-trip contract in one sentence.
std::string quote(const std::string &s)
{
	std::string out = "\"";
	for (char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\r':
			out += "\\r";
			break;
		default:
			out += c;
			break;
		}
	}
	out += '"';
	return out;
}

// Arrays go on one line while they fit comfortably, and one element per line
// once they do not. Both shapes parse back identically; the split exists so a
// manifest with thirty globs stays readable in a diff.
constexpr std::size_t kArrayWrapColumn = 80;

void render_array(std::ostringstream &out, const std::string &key,
	const std::vector<std::string> &values)
{
	std::string one_line = key + " = [";
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0) {
			one_line += ", ";
		}
		one_line += quote(values[i]);
	}
	one_line += "]";
	if (one_line.size() <= kArrayWrapColumn) {
		out << one_line << "\n";
		return;
	}
	out << key << " = [\n";
	for (const std::string &v : values) {
		out << "    " << quote(v) << ",\n";
	}
	out << "]\n";
}

} // namespace

bool rv_manifest_parse(const std::string &text, rv_manifest &out, std::string &error)
{
	error.clear();
	manifest_parser parser(text, out);
	return parser.run(error);
}

bool rv_manifest_load(const std::string &path, rv_manifest &out, std::string &error)
{
	error.clear();
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "cannot open manifest '" + path + "'";
		return false;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	if (in.bad()) {
		error = "cannot read manifest '" + path + "'";
		return false;
	}
	std::string message;
	if (!rv_manifest_parse(buffer.str(), out, message)) {
		// The line number is only useful next to the file it belongs to.
		error = path + ": " + message;
		return false;
	}
	return true;
}

std::string rv_manifest_render(const rv_manifest &manifest)
{
	std::ostringstream out;

	// Every key is written, including empty arrays and zero counts. A rendered
	// manifest is also the copy that goes onto the disc, and there it has to say
	// what the burner actually used rather than leaning on a default that a
	// later version of the tool might change.
	out << "[disc]\n";
	out << "id = " << quote(manifest.id) << "\n";
	out << "title = " << quote(manifest.title) << "\n";

	out << "\n[build]\n";
	render_array(out, "sources", manifest.sources);
	render_array(out, "defines", manifest.defines);
	render_array(out, "include_dirs", manifest.include_dirs);

	out << "\n[assets]\n";
	render_array(out, "files", manifest.files);

	out << "\n[textures]\n";
	render_array(out, "files", manifest.textures.files);
	out << "format = " << quote(manifest.textures.format) << "\n";

	out << "\n[budget]\n";
	out << "texture_max_width = " << manifest.budget.texture_max_width << "\n";
	out << "texture_max_height = " << manifest.budget.texture_max_height << "\n";
	out << "video_memory_size = " << manifest.budget.video_memory_size << "\n";

	return out.str();
}

bool rv_manifest_validate(const rv_manifest &manifest, std::string &error)
{
	error.clear();

	// The id is not decoration: it becomes the name of the burned file and of
	// the directory the console unpacks into, so it is checked as a FILENAME
	// before anything else touches it. A '/' would escape the output directory,
	// ".." would climb out of it, and a leading '.' would hide the result.
	if (manifest.id.empty()) {
		error = "[disc] id is empty — the disc needs a short machine name";
		return false;
	}
	for (char c : manifest.id) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) == 0 && c != '-' && c != '_') {
			error = "[disc] id '" + manifest.id +
				"' is not a safe filename — use letters, digits, '-' and '_' only";
			return false;
		}
	}
	if (manifest.id.front() == '.') { // unreachable through the loop above, kept as a guard
		error = "[disc] id must not start with '.'";
		return false;
	}

	bool format_known = false;
	for (std::string_view f : rv_burn_approved_texture_formats) {
		if (f == manifest.textures.format) {
			format_known = true;
		}
	}
	if (!format_known) {
		std::string known;
		for (std::size_t i = 0; i < std::size(rv_burn_approved_texture_formats); ++i) {
			if (i != 0) {
				known += ", ";
			}
			known += std::string(rv_burn_approved_texture_formats[i]);
		}
		error = "[textures] format '" + manifest.textures.format +
			"' is unknown — expected one of " + known;
		return false;
	}

	struct budget_field {
		const char *name;
		int64_t value;
	};
	const budget_field budgets[] = {
		{ "texture_max_width", manifest.budget.texture_max_width },
		{ "texture_max_height", manifest.budget.texture_max_height },
		{ "video_memory_size", manifest.budget.video_memory_size },
	};
	for (const budget_field &b : budgets) {
		if (b.value <= 0) {
			error = std::string("[budget] ") + b.name + " must be positive, got " +
				std::to_string(b.value);
			return false;
		}
	}

	return true;
}

} // namespace rv_pdktools
