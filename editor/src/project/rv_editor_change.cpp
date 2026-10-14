// Decides what a changed file does to the running session, from the burner's
// map alone: no filesystem access beyond lexical path comparison.

#include "project/rv_editor_change.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// C/C++ source file extensions that require rebuild on change.
constexpr std::string_view ext_c = ".c";
constexpr std::string_view ext_cc = ".cc";
constexpr std::string_view ext_cpp = ".cpp";
constexpr std::string_view ext_h = ".h";
constexpr std::string_view ext_hpp = ".hpp";

// Scene configuration file suffix.
constexpr std::string_view scene_config_suffix = ".scene.toml";

// Parent directory marker in normalized paths.
constexpr std::string_view parent_dir_marker = "..";

// Disc map entry kinds determine how file changes affect the running session.
constexpr std::string_view entry_kind_code = "code";
constexpr std::string_view entry_kind_entry = "entry";
constexpr std::string_view entry_kind_module = "module";
constexpr std::string_view entry_kind_texture = "texture";
constexpr std::string_view entry_kind_sound = "sound";

// Entry kind enum for dispatch.
enum class rv_editor_entry_kind {
    unknown,
    code,
    entry,
    module,
    texture,
    sound
};

// Entry kind name to enum lookup table.
constexpr struct {
    std::string_view name;
    rv_editor_entry_kind value;
} entry_kind_table[] = {
    { entry_kind_code, rv_editor_entry_kind::code },
    { entry_kind_entry, rv_editor_entry_kind::entry },
    { entry_kind_module, rv_editor_entry_kind::module },
    { entry_kind_texture, rv_editor_entry_kind::texture },
    { entry_kind_sound, rv_editor_entry_kind::sound },
};

// Parse entry kind via table lookup.
rv_editor_entry_kind parse_entry_kind(std::string_view kind)
{
    for (const auto &entry : entry_kind_table) {
        if (kind == entry.name) {
            return entry.value;
        }
    }
    return rv_editor_entry_kind::unknown;
}

bool rv_editor_change_is_cpp(const std::filesystem::path &path)
{
    static const std::array exts =
        std::array{ std::string(ext_c), std::string(ext_cc), std::string(ext_cpp), std::string(ext_h), std::string(ext_hpp) };
    const std::string ext = path.extension().string();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

bool rv_editor_change_is_scene(const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    if (name.size() < scene_config_suffix.size()) {
        return false;
    }
    return name.compare(name.size() - scene_config_suffix.size(), scene_config_suffix.size(), scene_config_suffix) == 0;
}

rv_editor_change_plan
rv_editor_change_plan_of(rv_editor_change_action action, std::string name, std::string parameter, std::string reason)
{
    return { action, std::move(name), std::move(parameter), std::move(reason) };
}

// Determine change plan for a map entry by its kind.
rv_editor_change_plan rv_editor_change_plan_for_entry(const rv_editor_map_entry &entry)
{
    const rv_editor_entry_kind kind = parse_entry_kind(entry.kind);
    switch (kind) {
    case rv_editor_entry_kind::code:
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart,
            "",
            "",
            rv_editor_text("change.save_and_rebuild"));
    case rv_editor_entry_kind::entry: {
        const auto args = std::make_format_args(entry.name);
        const auto reason = rv_editor_text_format("change.reload_entry_script", args);
        return rv_editor_change_plan_of(rv_editor_change_action::reload_entry, entry.name, "", reason);
    }
    case rv_editor_entry_kind::module: {
        const auto args = std::make_format_args(entry.parameter);
        const auto reason = rv_editor_text_format("change.reload_module", args);
        return rv_editor_change_plan_of(rv_editor_change_action::reload_module, entry.parameter, "", reason);
    }
    case rv_editor_entry_kind::texture: {
        const auto args = std::make_format_args(entry.name);
        const auto reason = rv_editor_text_format("change.refresh_texture", args);
        return rv_editor_change_plan_of(rv_editor_change_action::refresh_texture, entry.name, entry.parameter, reason);
    }
    case rv_editor_entry_kind::sound: {
        const auto args = std::make_format_args(entry.name);
        const auto reason = rv_editor_text_format("change.sound_restart_required", args);
        return rv_editor_change_plan_of(rv_editor_change_action::restart_required, entry.name, "", reason);
    }
    case rv_editor_entry_kind::unknown:
        break;
    }
    // "file", or any kind this editor does not know: only a restart picks it up.
    return rv_editor_change_plan_of(rv_editor_change_action::restart_required,
        entry.name,
        "",
        rv_editor_text("change.file_reload_not_supported"));
}

} // namespace

rv_editor_change_plan rv_editor_change_plan_for(const std::filesystem::path &root,
    const std::filesystem::path &manifest,
    const std::map<std::string, rv_editor_map_entry> &map,
    const std::filesystem::path &changed)
{
    if (map.empty()) {
        return rv_editor_change_plan_of(rv_editor_change_action::none, "", "", rv_editor_text("change.nothing_running"));
    }

    const std::filesystem::path changed_norm = changed.lexically_normal();

    if (changed_norm == manifest.lexically_normal()) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart,
            "",
            "",
            rv_editor_text("change.save_and_rebuild"));
    }
    if (rv_editor_change_is_scene(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart,
            "",
            "",
            rv_editor_text("change.scenes_reload_required"));
    }

    // Lexical, not resolved: root and changed must be spelled the same way, or
    // a symlinked root against an already-resolved path reads as outside it.
    const std::filesystem::path rel = changed_norm.lexically_relative(root.lexically_normal());
    const bool outside_root = rel.empty() || rel.begin()->string() == parent_dir_marker;
    if (outside_root) {
        return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "", rv_editor_text("change.not_in_project"));
    }

    const auto found = map.find(rel.generic_string());
    if (found != map.end()) {
        return rv_editor_change_plan_for_entry(found->second);
    }

    if (rv_editor_change_is_cpp(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart,
            "",
            "",
            rv_editor_text("change.save_and_rebuild"));
    }
    return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "", rv_editor_text("change.not_on_disc"));
}

} // namespace rv_editor
