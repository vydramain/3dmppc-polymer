#include "rv_burner_manifest_parser.hpp"

#include <string>
#include <utility>

#include "rv_burner_manifest_token.hpp"
#include "rv_burner_manifest_tree.hpp"
#include "rv_burner_manifest_value.hpp"

namespace rv_pdktools
{
namespace
{

using tk = rv_burner_token_kind;

} // namespace

rv_burner_tree rv_burner_manifest_parser::run()
{
	while (!at(tk::END_OF_FILE) && !failer_.exhausted()) {
		if (at(tk::NEWLINE)) {
			get();
			continue;
		}
		if (at(tk::INVALID)) {
			failer_.fail(peek().line, peek().text);
			recover();
			continue;
		}
		if (at(tk::LBRACKET)) {
			if (!parse_section()) {
				recover();
			}
			continue;
		}
		if (at(tk::IDENT)) {
			if (!parse_assignment()) {
				recover();
			}
			continue;
		}
		failer_.fail(peek().line,
			"expected a section header or 'key = value', found " + rv_burner_token_spelling(peek()));
		recover();
	}
	return std::move(tree_);
}

// --- cursor -------------------------------------------------------------------

const rv_burner_token &rv_burner_manifest_parser::peek() const
{
	return tokens_[pos_];
}

// The stream ends in exactly one END_OF_FILE and the cursor parks on it, so no
// caller can walk off the end.
const rv_burner_token &rv_burner_manifest_parser::get()
{
	const rv_burner_token &token = tokens_[pos_];
	if (token.kind != tk::END_OF_FILE) {
		++pos_;
	}
	return token;
}

bool rv_burner_manifest_parser::at(rv_burner_token_kind kind) const
{
	return peek().kind == kind;
}

// --- grammar ------------------------------------------------------------------

bool rv_burner_manifest_parser::parse_section()
{
	const int line = peek().line;
	get(); // '['
	if (!at(tk::IDENT)) {
		open_section(std::string(), line, true);
		return failer_.fail(line,
			at(tk::RBRACKET) ? "empty section header" : "expected a section name after '['");
	}
	std::string name = get().text;
	if (!at(tk::RBRACKET)) {
		open_section(name, line, true);
		return failer_.fail(line, "section header '[" + name + "' is missing its closing ']'");
	}
	get(); // ']'
	open_section(std::move(name), line, false);
	return expect_line_end("section header");
}

bool rv_burner_manifest_parser::parse_assignment()
{
	const std::string key = peek().text;
	const int line = get().line;
	if (!at(tk::EQUALS)) {
		return failer_.fail(line, "expected '=' after key '" + key + "'");
	}
	get(); // '='
	if (tree_.sections.empty()) {
		failer_.fail(line, "key '" + key + "' appears before any section header");
		// Reported once: everything before the first header lands in a section
		// nobody will judge.
		open_section(std::string(), line, true);
	}

	rv_burner_tree_entry entry;
	entry.key = key;
	entry.line = line;
	if (!parse_value(entry.value)) {
		return false;
	}
	current_section().entries.push_back(std::move(entry));
	return expect_line_end("value");
}

bool rv_burner_manifest_parser::parse_value(rv_burner_mvalue &out)
{
	const rv_burner_token &token = peek();
	out.line = token.line;
	switch (token.kind) {
	case tk::STRING:
		out.kind = rv_burner_value_kind::string;
		out.str = get().text;
		return true;
	case tk::INTEGER:
		out.kind = rv_burner_value_kind::integer;
		out.num = get().num;
		return true;
	case tk::LBRACKET:
		out.kind = rv_burner_value_kind::array;
		return parse_array(out);
	case tk::INVALID:
		return failer_.fail(token.line, token.text);
	case tk::NEWLINE:
	case tk::END_OF_FILE:
		return failer_.fail(token.line, "missing value after '='");
	case tk::IDENT:
		if (token.text == "true" || token.text == "false") {
			return failer_.fail(token.line,
				"booleans are not supported — this manifest holds strings, "
				"integers and arrays of strings only");
		}
		break;
	default:
		break;
	}
	return failer_.fail(token.line,
		"unsupported value " + rv_burner_token_spelling(token) +
			" — expected a quoted string, an integer or an array of quoted strings");
}

// Newlines inside the brackets are skipped, which is the whole of the
// multi-line array support: both shapes parse to the same value.
bool rv_burner_manifest_parser::parse_array(rv_burner_mvalue &out)
{
	const int start = get().line; // '['
	for (;;) {
		while (at(tk::NEWLINE)) {
			get();
		}
		if (at(tk::END_OF_FILE)) {
			return failer_.fail(start, "unterminated array — no closing ']' before end of file");
		}
		if (at(tk::RBRACKET)) {
			get();
			return true;
		}
		if (at(tk::INVALID)) {
			return failer_.fail(peek().line, peek().text);
		}
		if (at(tk::COMMA)) {
			return failer_.fail(peek().line, "empty element in array — expected a quoted string");
		}
		if (!at(tk::STRING)) {
			return failer_.fail(peek().line, "array elements must be quoted strings");
		}
		out.arr.push_back(get().text);

		while (at(tk::NEWLINE)) {
			get();
		}
		if (at(tk::COMMA)) {
			get();
			continue;
		}
		if (at(tk::RBRACKET)) {
			get();
			return true;
		}
		if (at(tk::END_OF_FILE)) {
			return failer_.fail(start, "unterminated array — no closing ']' before end of file");
		}
		return failer_.fail(peek().line,
			"expected ',' or ']' in array, found " + rv_burner_token_spelling(peek()));
	}
}

bool rv_burner_manifest_parser::expect_line_end(const char *what)
{
	if (at(tk::END_OF_FILE)) {
		return true;
	}
	if (at(tk::NEWLINE)) {
		get();
		return true;
	}
	return failer_.fail(peek().line,
		std::string("unexpected text after ") + what + " — one " + what + " per line");
}

// --- recovery -----------------------------------------------------------------

void rv_burner_manifest_parser::recover()
{
	for (;;) {
		while (!at(tk::END_OF_FILE) && !at(tk::NEWLINE)) {
			get();
		}
		if (at(tk::END_OF_FILE)) {
			return;
		}
		get(); // the newline
		if (starts_statement()) {
			return;
		}
	}
}

bool rv_burner_manifest_parser::starts_statement() const
{
	if (at(tk::END_OF_FILE) || at(tk::NEWLINE) || at(tk::LBRACKET)) {
		return true;
	}
	return at(tk::IDENT) && tokens_[pos_ + 1].kind == tk::EQUALS;
}

void rv_burner_manifest_parser::open_section(std::string name, int line, bool poisoned)
{
	rv_burner_tree_section section;
	section.name = std::move(name);
	section.line = line;
	section.poisoned = poisoned;
	tree_.sections.push_back(std::move(section));
}

rv_burner_tree_section &rv_burner_manifest_parser::current_section()
{
	return tree_.sections.back();
}

} // namespace rv_pdktools
