// Console UI texts from the embedded file only; a translation is a rebuild.

#include "rv_pconsole/rv_pctexts.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

#include "pdk/rv_err.h"

#include "rv_console_texts_default.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"

namespace rv_3dmppc
{

namespace
{

// Log origin tag of this module's messages.
inline constexpr const char *rv_pctexts_log_tag = "pctexts";

// Source name in parser diagnostics: says the text is the built-in copy.
inline constexpr const char *rv_pctexts_source_name = "rv_console_texts.toml (built in)";

// Joins section and key into the id that callers pass to rv_pctext.
inline constexpr char rv_pctexts_key_separator = '.';

// "section.key" -> text. Only used from the main thread.
std::unordered_map<std::string, std::string> g_texts;

} // namespace

int rv_pctexts_load()
{
    g_texts.clear();

    const std::string content(rv_console_texts_default);
    rv_pdklib::rv_manifest_tree tree;
    std::string error;
    if (rv_pdklib::rv_manifest_read_tree(content, rv_pctexts_source_name, tree, error) != 0) {
        RV_LOG_ERR(rv_pctexts_log_tag, "{}", error);
        return RV_ERR_INVAL;
    }

    for (const auto &section : tree.sections) {
        for (const auto &entry : section.entries) {
            const std::string id = section.name + rv_pctexts_key_separator + entry.key;
            if (entry.value.kind != rv_pdklib::rv_manifest_value_kind::string) {
                RV_LOG_WARN(rv_pctexts_log_tag,
                    "'{}' is ignored: its value is not a string",
                    rv_pdklib::rv_log_escape(id.c_str()));
                continue;
            }
            g_texts[id] = entry.value.str;
        }
    }

    return RV_OK;
}

const char *rv_pctext(std::string_view id)
{
    const std::string key(id);
    auto it = g_texts.find(key);
    if (it != g_texts.end()) {
        return it->second.c_str();
    }

    // Stored as its own text, so the next lookup of this id finds it and does not warn again.
    RV_LOG_WARN(rv_pctexts_log_tag, "no text for id '{}', showing the id", rv_pdklib::rv_log_escape(key.c_str()));
    return g_texts.emplace(key, key).first->second.c_str();
}

} // namespace rv_3dmppc
