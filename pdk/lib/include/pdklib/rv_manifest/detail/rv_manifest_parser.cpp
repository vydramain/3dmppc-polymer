#include "rv_manifest_parser.hpp"

#include <string>
#include <utility>

#include "pdk/rv_err.h"
#include "rv_manifest_token.hpp"
#include "rv_manifest_tree.hpp"
#include "rv_manifest_value.hpp"

namespace rv_pdklib
{
namespace
{

using tk = rv_manifest_token_kind;

} // namespace

rv_manifest_tree rv_manifest_parser::run()
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
            if (parse_section() != RV_OK) {
                recover();
            }
            continue;
        }
        if (at(tk::IDENT)) {
            if (parse_assignment() != RV_OK) {
                recover();
            }
            continue;
        }
        failer_.fail(peek().line,
            "expected a section header or 'key = value', found " + rv_manifest_token_spelling(peek()));
        recover();
    }
    return std::move(tree_);
}

// --- cursor -------------------------------------------------------------------

const rv_manifest_token &rv_manifest_parser::peek() const
{
    return tokens_[pos_];
}

// The stream ends in exactly one END_OF_FILE and the cursor parks on it, so no
// caller can walk off the end.
const rv_manifest_token &rv_manifest_parser::get()
{
    const rv_manifest_token &token = tokens_[pos_];
    if (token.kind != tk::END_OF_FILE) {
        ++pos_;
    }
    return token;
}

bool rv_manifest_parser::at(rv_manifest_token_kind kind) const
{
    return peek().kind == kind;
}

// --- grammar ------------------------------------------------------------------

int rv_manifest_parser::parse_section()
{
    const int line = peek().line;
    get(); // '['
    // [[name]] opens one more table of an array of them.
    const bool array = at(tk::LBRACKET);
    if (array) {
        get();
    }
    const std::string open = array ? "[[" : "[";
    if (!at(tk::IDENT)) {
        open_section(std::string(), line, true);
        return failer_.fail(line,
            at(tk::RBRACKET) ? std::string("empty section header") : "expected a section name after '" + open + "'");
    }
    std::string name = get().text;
    const bool closed = at(tk::RBRACKET) && (!array || tokens_[pos_ + 1].kind == tk::RBRACKET);
    if (!closed) {
        open_section(name, line, true);
        return failer_.fail(line, "section header '" + open + name + "' is missing its closing " +
            (array ? "']]'" : "']'"));
    }
    get(); // ']'
    if (array) {
        get(); // ']'
    }
    open_section(std::move(name), line, false);
    current_section().array = array;
    return expect_line_end("section header");
}

int rv_manifest_parser::parse_assignment()
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

    rv_manifest_tree_entry entry;
    entry.key = key;
    entry.line = line;
    if (parse_value(entry.value) != RV_OK) {
        return RV_ERR_INVAL;
    }
    current_section().entries.push_back(std::move(entry));
    return expect_line_end("value");
}

int rv_manifest_parser::parse_value(rv_manifest_mvalue &out)
{
    const rv_manifest_token &token = peek();
    out.line = token.line;
    switch (token.kind) {
    case tk::STRING:
        out.kind = rv_manifest_value_kind::string;
        out.str = get().text;
        return RV_OK;
    case tk::INTEGER:
        out.kind = rv_manifest_value_kind::integer;
        out.num = get().num;
        return RV_OK;
    case tk::REAL:
        out.kind = rv_manifest_value_kind::real;
        out.real = get().real;
        return RV_OK;
    case tk::LBRACKET:
        out.kind = rv_manifest_value_kind::array;
        return parse_array(out);
    case tk::INVALID:
        return failer_.fail(token.line, token.text);
    case tk::NEWLINE:
    case tk::END_OF_FILE:
        return failer_.fail(token.line, "missing value after '='");
    case tk::IDENT:
        if (token.text == "true" || token.text == "false") {
            return failer_.fail(token.line,
                "booleans are not supported — this dialect holds strings, numbers and "
                "arrays of either");
        }
        break;
    default:
        break;
    }
    return failer_.fail(token.line,
        "unsupported value " + rv_manifest_token_spelling(token) +
            " — expected a quoted string, a number or an array");
}

// Newlines inside the brackets are skipped, which is the whole of the
// multi-line array support: both shapes parse to the same value.
int rv_manifest_parser::parse_array(rv_manifest_mvalue &out)
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
            return RV_OK;
        }
        if (at(tk::INVALID)) {
            return failer_.fail(peek().line, peek().text);
        }
        if (at(tk::COMMA)) {
            return failer_.fail(peek().line, "empty element in array — expected a value");
        }
        // The first element decides: strings, or numbers; never both.
        const bool number = at(tk::INTEGER) || at(tk::REAL);
        if (!number && !at(tk::STRING)) {
            return failer_.fail(peek().line, "array elements must be quoted strings or numbers");
        }
        const bool first = out.arr.empty() && out.nums.empty();
        if (first && number) {
            out.kind = rv_manifest_value_kind::numbers;
        }
        if (number != (out.kind == rv_manifest_value_kind::numbers)) {
            return failer_.fail(peek().line, "an array holds strings or numbers, not both");
        }
        if (number) {
            const rv_manifest_token &t = get();
            out.nums.push_back(t.kind == tk::REAL ? t.real : static_cast<double>(t.num));
        } else {
            out.arr.push_back(get().text);
        }

        while (at(tk::NEWLINE)) {
            get();
        }
        if (at(tk::COMMA)) {
            get();
            continue;
        }
        if (at(tk::RBRACKET)) {
            get();
            return RV_OK;
        }
        if (at(tk::END_OF_FILE)) {
            return failer_.fail(start, "unterminated array — no closing ']' before end of file");
        }
        return failer_.fail(peek().line,
            "expected ',' or ']' in array, found " + rv_manifest_token_spelling(peek()));
    }
}

int rv_manifest_parser::expect_line_end(const char *what)
{
    if (at(tk::END_OF_FILE)) {
        return RV_OK;
    }
    if (at(tk::NEWLINE)) {
        get();
        return RV_OK;
    }
    return failer_.fail(peek().line,
        std::string("unexpected text after ") + what + " — one " + what + " per line");
}

// --- recovery -----------------------------------------------------------------

void rv_manifest_parser::recover()
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

bool rv_manifest_parser::starts_statement() const
{
    if (at(tk::END_OF_FILE) || at(tk::NEWLINE) || at(tk::LBRACKET)) {
        return true;
    }
    return at(tk::IDENT) && tokens_[pos_ + 1].kind == tk::EQUALS;
}

void rv_manifest_parser::open_section(std::string name, int line, bool poisoned)
{
    rv_manifest_tree_section section;
    section.name = std::move(name);
    section.line = line;
    section.poisoned = poisoned;
    tree_.sections.push_back(std::move(section));
}

rv_manifest_tree_section &rv_manifest_parser::current_section()
{
    return tree_.sections.back();
}

} // namespace rv_pdklib
