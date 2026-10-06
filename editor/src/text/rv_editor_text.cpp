// Editor UI text strings: embedded default and optional user overrides.

#include "text/rv_editor_text.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "pdk/rv_err.h"

#include "rv_editor_texts_default.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"

namespace rv_editor
{

namespace
{

// Text entries as "section.key" -> text. Only used from UI thread.
std::unordered_map<std::string, std::string> g_texts;

// Parse TOML content and add entries to g_texts. Returns RV_OK, or RV_ERR_INVAL with
// the reason in `error`. Strings only, other kinds ignored; any string key becomes an
// entry ID as "section.key".
int rv_editor_text_parse(const std::string &content, const std::string &origin,
    std::string &error)
{
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(content, origin, tree, error) != 0) {
        return RV_ERR_INVAL;
    }

    for (const auto &section : tree.sections) {
        for (const auto &entry : section.entries) {
            if (entry.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                std::string id = section.name + "." + entry.key;
                g_texts[id] = entry.value.str;
            }
        }
    }

    return RV_OK;
}

} // namespace

int rv_editor_text_load(const std::filesystem::path &user_file, std::string &error)
{
    g_texts.clear();

    // Load embedded default.
    std::string default_content(rv_editor_texts_default);
    int result = rv_editor_text_parse(default_content, "rv_editor_texts.toml (built in)",
        error);
    if (result != RV_OK) {
        return result;
    }

    // Load user file if provided.
    if (!user_file.empty()) {
        std::ifstream in(user_file, std::ios::binary);
        if (!in) {
            // No user file is not an error.
            if (in.fail() && std::filesystem::exists(user_file)) {
                error = "cannot read: " + user_file.string();
                return RV_ERR_IO;
            }
            return RV_OK;
        }

        std::ostringstream buf;
        buf << in.rdbuf();
        std::string user_content = buf.str();

        int result = rv_editor_text_parse(user_content, user_file.string(), error);
        if (result != RV_OK) {
            return result;
        }
    }

    return RV_OK;
}

const char *rv_editor_text(std::string_view id)
{
    std::string id_str(id);
    auto it = g_texts.find(id_str);
    if (it != g_texts.end()) {
        return it->second.c_str();
    }

    // Insert and return the ID itself for unknown entries.
    return g_texts.emplace(id_str, id_str).first->second.c_str();
}

std::string rv_editor_text_format(std::string_view id, std::format_args args)
{
    const char *template_text = rv_editor_text(id);
    try {
        return std::vformat(template_text, args);
    } catch (const std::format_error &) {
        // Return template text unchanged on invalid placeholders.
        return std::string(template_text);
    }
}

} // namespace rv_editor
