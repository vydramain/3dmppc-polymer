// settings.toml written back: only the [tools] section changes, so what the user
// wrote by hand elsewhere in the file stays.

#include "project/rv_editor_settings.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace rv_editor
{

namespace
{

// The escapes the disc.toml lexer reads back, and no others.
std::string rv_editor_settings_quote(std::string_view s)
{
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out + "\"";
}

// True for a "[name]" header line, with the name between the brackets.
bool rv_editor_settings_header(std::string_view line, std::string_view &name)
{
    const size_t open = line.find_first_not_of(" \t");
    if (open == std::string_view::npos || line[open] != '[') {
        return false;
    }
    const size_t close = line.find(']', open);
    if (close == std::string_view::npos) {
        return false;
    }
    name = line.substr(open + 1, close - open - 1);
    const size_t first = name.find_first_not_of(" \t");
    name = first == std::string_view::npos ? std::string_view() : name.substr(first, name.find_last_not_of(" \t") - first + 1);
    return true;
}

} // namespace

std::string rv_editor_settings_with_tools(std::string_view text, const std::vector<rv_editor_settings_tool> &tools)
{
    std::string section = "[tools]\n";
    for (const auto &[key, path] : tools) {
        if (!path.empty()) {
            section += key + " = " + rv_editor_settings_quote(path) + "\n";
        }
    }
    std::string out;
    bool in_tools = false;
    bool written = false;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        const std::string_view line = text.substr(at, end - at);
        at = end;
        std::string_view name;
        if (rv_editor_settings_header(line, name)) {
            in_tools = name == "tools";
            if (in_tools && !written) {
                out += section + "\n";
                written = true;
            }
        }
        if (in_tools) {
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

bool rv_editor_settings_save_tools(const std::filesystem::path &path, const std::vector<rv_editor_settings_tool> &tools,
    std::string &error)
{
    std::string text;
    if (std::ifstream in(path, std::ios::binary); in) {
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << rv_editor_settings_with_tools(text, tools);
    out.close();
    if (!out) {
        error = "cannot write " + tmp.string();
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace rv_editor
