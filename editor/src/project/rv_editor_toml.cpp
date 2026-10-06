// The editor's own files: quoting for disc.toml's dialect and atomic replacement.

#include "project/rv_editor_toml.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Temporary-file suffix for atomic replacement.
constexpr std::string_view temp_file_suffix = ".tmp";

} // namespace

std::string rv_editor_toml_quote(std::string_view s)
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

int rv_editor_file_replace(const std::filesystem::path &path, const std::string &text, std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + std::string(temp_file_suffix);
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        error = "cannot write " + tmp.string();
        return RV_ERR_IO;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

std::string rv_editor_file_text(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

} // namespace rv_editor
