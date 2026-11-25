// settings.toml section updates preserve the other sections.

#include "project/rv_editor_settings.hpp"

#include <string_view>

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

constexpr std::string_view leading_blanks_chars = " \t"; // whitespace to trim
constexpr std::string_view section_name_tools = "tools";
constexpr std::string_view section_name_shortcuts = "shortcuts";

// True for a "[name]" header line, with the name between the brackets.
bool rv_editor_settings_header(std::string_view line, std::string_view &name)
{
    const size_t open = line.find_first_not_of(leading_blanks_chars);
    if (open == std::string_view::npos || line[open] != '[') {
        return false;
    }
    const size_t close = line.find(']', open);
    if (close == std::string_view::npos) {
        return false;
    }
    name = line.substr(open + 1, close - open - 1);
    const size_t first = name.find_first_not_of(leading_blanks_chars);
    if (first != std::string_view::npos) {
        const size_t last = name.find_last_not_of(leading_blanks_chars);
        name = name.substr(first, last - first + 1);
    } else {
        name = std::string_view();
    }
    return true;
}

std::string
rv_editor_settings_with_section(std::string_view text, std::string_view section_name, const rv_editor_settings_entries &entries)
{
    std::string section = "[" + std::string(section_name) + "]\n";
    for (const auto &[key, value] : entries) {
        if (!value.empty()) {
            section += key + " = " + rv_editor_toml_quote(value) + "\n";
        }
    }
    std::string out;
    bool in_section = false;
    bool written = false;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        const std::string_view line = text.substr(at, end - at);
        at = end;
        std::string_view name;
        if (rv_editor_settings_header(line, name)) {
            in_section = name == section_name;
            if (in_section && !written) {
                out += section + "\n";
                written = true;
            }
        }
        if (in_section) {
            continue;
        }
        out += line;
        if (nl == std::string_view::npos) {
            out += '\n';
        }
    }
    if (!written) {
        out += (out.empty() ? "" : "\n") + section;
    }
    return out;
}

} // namespace

rv_editor_settings_entries rv_editor_settings_read_shortcuts(const std::filesystem::path &path)
{
    const std::string text = rv_editor_file_text(path);
    rv_pdklib::rv_manifest_tree tree;
    std::string error;
    if (text.empty() || rv_pdklib::rv_manifest_read_tree(text, path.string(), tree, error) != 0) {
        return {};
    }
    rv_editor_settings_entries entries;
    for (const auto &section : tree.sections) {
        if (section.name == section_name_shortcuts) {
            for (const auto &entry : section.entries) {
                if (entry.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                    entries.emplace_back(entry.key, entry.value.str);
                }
            }
        }
    }
    return entries;
}

int rv_editor_settings_save(const std::filesystem::path &path,
    const rv_editor_settings_entries &tools,
    const std::optional<rv_editor_settings_entries> &shortcuts,
    std::string &error)
{
    std::string text = rv_editor_settings_with_section(rv_editor_file_text(path), section_name_tools, tools);
    if (shortcuts) {
        text = rv_editor_settings_with_section(text, section_name_shortcuts, *shortcuts);
    }
    return rv_editor_file_replace(path, text, error);
}

} // namespace rv_editor
