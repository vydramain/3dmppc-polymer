// Adds a pattern to a disc.toml-dialect manifest section by editing the
// original text in place, using the tree's line numbers to find the spot.

#include "project/rv_editor_manifest_edit.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

// TOML manifest key for file patterns in a section
constexpr std::string_view manifest_key_files = "files";
// Line ending: CRLF detected from file format to preserve Windows newlines
constexpr std::string_view manifest_eol_crlf = "\r\n";
// Line ending: LF fallback for Unix/unknown formats
constexpr std::string_view manifest_eol_lf = "\n";
// Array element separator in TOML format
constexpr std::string_view manifest_array_separator = ",";

// starts[n] is the byte offset where line n (1-based, matching tree line
// numbers) begins. A trailing line with no final '\n' has no entry past it.
std::vector<size_t> rv_manifest_edit_line_starts(const std::string &text)
{
    std::vector<size_t> starts = { 0, 0 }; // index 0 unused, line 1 starts at 0
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

// Offset of the ']' matching the '[' at `open`, skipping brackets inside
// quoted strings (with the dialect's backslash escapes) and inside '#'
// comments. npos if unbalanced.
size_t rv_manifest_edit_array_close(const std::string &text, size_t open)
{
    int depth = 1;
    bool in_string = false;
    for (size_t i = open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                ++i; // the escaped character, whatever it is
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '#') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']' && --depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

// The dialect's own line ending, sniffed from the file's first newline: CRLF
// stays CRLF, everything else (including no newline at all) is LF.
std::string rv_manifest_edit_eol(const std::string &text)
{
    const size_t nl = text.find('\n');
    if (nl != std::string::npos && nl > 0 && text[nl - 1] == '\r') {
        return std::string(manifest_eol_crlf);
    }
    return std::string(manifest_eol_lf);
}

size_t rv_manifest_edit_indent_end(const std::string &text, size_t line_start)
{
    size_t k = line_start;
    while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) {
        ++k;
    }
    return k;
}

// Start of the line containing `pos` (which may itself be the '\n' ending it).
size_t rv_manifest_edit_line_start_of(const std::string &text, size_t pos)
{
    if (pos == 0) {
        return 0;
    }
    const size_t nl = text.rfind('\n', pos - 1);
    return nl == std::string::npos ? 0 : nl + 1;
}

// End of the line starting at `start`: before its '\n' and, for CRLF, before
// the '\r' too. Never past `text.size()`.
size_t rv_manifest_edit_line_end(const std::string &text, size_t start)
{
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
        end = text.size();
    }
    if (end > start && text[end - 1] == '\r') {
        --end;
    }
    return end;
}

// First '#' in [start, end) outside a quoted string, or npos.
size_t rv_manifest_edit_find_hash(const std::string &text, size_t start, size_t end)
{
    bool in_string = false;
    for (size_t i = start; i < end; ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '#') {
            return i;
        }
    }
    return std::string::npos;
}

// Whether [start, end) holds nothing but whitespace and, optionally, a comment.
bool rv_manifest_edit_blank_or_comment(const std::string &text, size_t start, size_t end)
{
    size_t i = start;
    while (i < end && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    return i == end || text[i] == '#';
}

// Find section by name in tree, or nullptr.
const rv_pdklib::rv_manifest_tree_section *find_section(const rv_pdklib::rv_manifest_tree &tree, std::string_view section)
{
    for (const auto &s : tree.sections) {
        if (!s.poisoned && !s.array && s.name == section) {
            return &s;
        }
    }
    return nullptr;
}

// Find "files" entry in section, or nullptr.
const rv_pdklib::rv_manifest_tree_entry *find_files_entry(const rv_pdklib::rv_manifest_tree_section &sec)
{
    for (const auto &e : sec.entries) {
        if (e.key == manifest_key_files) {
            return &e;
        }
    }
    return nullptr;
}

// Add section with files array containing one element.
void add_section(std::string &text, std::string_view section, const std::string &quoted, const std::string &eol)
{
    text += eol + "[" + std::string(section) + "]" + eol + "files = [" + quoted + "]" + eol;
}

// Insert files entry into existing section after its header.
void insert_files_entry(std::string &text,
    const rv_pdklib::rv_manifest_tree_section &sec,
    const std::string &quoted,
    const std::string &eol)
{
    const std::vector<size_t> starts = rv_manifest_edit_line_starts(text);
    const size_t after_header = static_cast<size_t>(sec.line) + 1;
    const size_t insert_at = after_header < starts.size() ? starts[after_header] : text.size();
    const bool need_nl = insert_at == text.size() && !text.empty() && text.back() != '\n';
    text.insert(insert_at, (need_nl ? eol : std::string()) + "files = [" + quoted + "]" + eol);
}

// Appends `pattern` (already quoted) to an existing files array spanning
// [open, close] in `text`. Rewrites `text` in place.
void rv_manifest_edit_append_element(std::string &text,
    size_t open,
    size_t close,
    bool was_empty,
    const std::string &quoted,
    const std::string &eol)
{
    const bool single_line = text.find('\n', open) == std::string::npos || text.find('\n', open) > close;
    if (single_line) {
        if (was_empty) {
            text.insert(open + 1, quoted);
        } else {
            text.insert(close, ", " + quoted);
        }
        return;
    }
    const size_t close_line_start = rv_manifest_edit_line_start_of(text, close);
    if (was_empty) {
        const size_t indent_end = rv_manifest_edit_indent_end(text, close_line_start);
        const std::string indent = text.substr(close_line_start, indent_end - close_line_start);
        text.insert(close_line_start, indent + quoted + eol);
        return;
    }
    // Walk up from the closing bracket past blank and comment-only lines to
    // the line that actually holds the last element.
    size_t cursor = close_line_start;
    size_t elem_start = std::string::npos;
    size_t elem_end = 0;
    while (cursor > 0) {
        const size_t line_start = rv_manifest_edit_line_start_of(text, cursor - 1);
        const size_t content_end = rv_manifest_edit_line_end(text, line_start);
        if (!rv_manifest_edit_blank_or_comment(text, line_start, content_end)) {
            elem_start = line_start;
            elem_end = content_end;
            break;
        }
        cursor = line_start;
    }
    if (elem_start == std::string::npos) {
        // No element line found (should not happen when was_empty is false);
        // fall back to the closing bracket's own indentation.
        const size_t indent_end = rv_manifest_edit_indent_end(text, close_line_start);
        const std::string indent = text.substr(close_line_start, indent_end - close_line_start);
        text.insert(close_line_start, indent + quoted + eol);
        return;
    }
    const size_t indent_end = rv_manifest_edit_indent_end(text, elem_start);
    const std::string indent = text.substr(elem_start, indent_end - elem_start);
    const size_t hash = rv_manifest_edit_find_hash(text, elem_start, elem_end);
    size_t value_end = hash == std::string::npos ? elem_end : hash;
    while (value_end > elem_start && (text[value_end - 1] == ' ' || text[value_end - 1] == '\t')) {
        --value_end;
    }
    const bool has_comma = value_end > elem_start && text[value_end - 1] == ',';
    // The new line goes right after the element line's own newline; insert it
    // first (higher offset) so the comma's offset below stays valid.
    const size_t next_nl = text.find('\n', elem_end);
    const size_t insert_at = next_nl == std::string::npos ? text.size() : next_nl + 1;
    text.insert(insert_at, indent + quoted + eol);
    if (!has_comma) {
        text.insert(value_end, std::string(manifest_array_separator));
    }
}

// Write text, verify by reloading, restore original on failure.
int write_checked(const std::filesystem::path &manifest,
    const std::string &original,
    const std::string &text,
    std::string &error)
{
    const int write_code = rv_editor_file_replace(manifest, text, error);
    if (write_code != RV_OK) {
        return write_code;
    }
    rv_pdklib::rv_manifest check;
    std::string load_error;
    if (rv_pdklib::rv_manifest_load(manifest.string(), check, load_error) == 0) {
        return RV_OK;
    }
    std::string restore_error;
    const int restore_code = rv_editor_file_replace(manifest, original, restore_error);
    error = restore_code != RV_OK ? load_error + "; the original could not be restored either: " + restore_error : load_error;
    return RV_ERR_INVAL;
}

} // namespace

int rv_editor_manifest_add_pattern(const std::filesystem::path &manifest,
    std::string_view section,
    std::string_view pattern,
    std::string &error)
{
    std::ifstream in(manifest, std::ios::binary);
    if (!in) {
        error = manifest.string() + ": cannot read";
        return RV_ERR_IO;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string original = buffer.str();
    in.close();

    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(original, manifest.string(), tree, error) != 0) {
        return RV_ERR_INVAL;
    }

    const std::string quoted = rv_editor_toml_quote(pattern);
    const std::string eol = rv_manifest_edit_eol(original);
    std::string text = original;

    const auto *sec = find_section(tree, section);
    if (sec == nullptr) {
        add_section(text, section, quoted, eol);
        return write_checked(manifest, original, text, error);
    }

    const auto *files = find_files_entry(*sec);
    if (files == nullptr) {
        insert_files_entry(text, *sec, quoted, eol);
        return write_checked(manifest, original, text, error);
    }

    if (files->value.kind != rv_pdklib::rv_manifest_value_kind::array) {
        error = manifest.string() + ": '" + std::string(section) + ".files' is not an array";
        return RV_ERR_INVAL;
    }

    for (const std::string &existing : files->value.arr) {
        if (existing == pattern) {
            return RV_OK;
        }
    }

    const std::vector<size_t> starts = rv_manifest_edit_line_starts(text);
    const size_t open = text.find('[', starts[static_cast<size_t>(files->value.line)]);
    const size_t close = open == std::string::npos ? std::string::npos : rv_manifest_edit_array_close(text, open);
    if (open == std::string::npos || close == std::string::npos) {
        error = manifest.string() + ": could not locate '" + std::string(section) + ".files' in the text";
        return RV_ERR_INVAL;
    }

    rv_manifest_edit_append_element(text, open, close, files->value.arr.empty(), quoted, eol);
    return write_checked(manifest, original, text, error);
}

} // namespace rv_editor
