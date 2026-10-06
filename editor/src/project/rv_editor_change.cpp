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

bool rv_editor_change_is_cpp(const std::filesystem::path &path)
{
    static const std::array exts = std::array{
        std::string(ext_c), std::string(ext_cc), std::string(ext_cpp),
        std::string(ext_h), std::string(ext_hpp)
    };
    const std::string ext = path.extension().string();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

bool rv_editor_change_is_scene(const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    if (name.size() < scene_config_suffix.size()) {
        return false;
    }
    return name.compare(name.size() - scene_config_suffix.size(), scene_config_suffix.size(),
               scene_config_suffix) == 0;
}

rv_editor_change_plan rv_editor_change_plan_of(rv_editor_change_action action, std::string name,
    std::string parameter, std::string reason)
{
    return { action, std::move(name), std::move(parameter), std::move(reason) };
}

} // namespace

rv_editor_change_plan rv_editor_change_plan_for(const std::filesystem::path &root,
    const std::filesystem::path &manifest,
    const std::map<std::string, rv_editor_map_entry> &map,
    const std::filesystem::path &changed)
{
    if (map.empty()) {
        return rv_editor_change_plan_of(rv_editor_change_action::none, "", "",
            rv_editor_text("change.nothing_running"));
    }

    const std::filesystem::path changed_norm = changed.lexically_normal();

    if (changed_norm == manifest.lexically_normal()) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "",
            rv_editor_text("change.save_and_rebuild"));
    }
    if (rv_editor_change_is_scene(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "",
            rv_editor_text("change.scenes_reload_required"));
    }

    // Lexical, not resolved: root and changed must be spelled the same way, or
    // a symlinked root against an already-resolved path reads as outside it.
    const std::filesystem::path rel = changed_norm.lexically_relative(root.lexically_normal());
    const bool outside_root = rel.empty() || rel.begin()->string() == parent_dir_marker;
    if (outside_root) {
        return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "",
            rv_editor_text("change.not_in_project"));
    }

    const auto found = map.find(rel.generic_string());
    if (found != map.end()) {
        const rv_editor_map_entry &entry = found->second;
        if (entry.kind == entry_kind_code) {
            return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "",
                rv_editor_text("change.save_and_rebuild"));
        }
        if (entry.kind == entry_kind_entry) {
            const auto args = std::make_format_args(entry.name);
            const auto reason = rv_editor_text_format("change.reload_entry_script", args);
            return rv_editor_change_plan_of(rv_editor_change_action::reload_entry, entry.name, "", reason);
        }
        if (entry.kind == entry_kind_module) {
            const auto args = std::make_format_args(entry.parameter);
            const auto reason = rv_editor_text_format("change.reload_module", args);
            return rv_editor_change_plan_of(rv_editor_change_action::reload_module, entry.parameter, "", reason);
        }
        if (entry.kind == entry_kind_texture) {
            const auto args = std::make_format_args(entry.name);
            const auto reason = rv_editor_text_format("change.refresh_texture", args);
            return rv_editor_change_plan_of(rv_editor_change_action::refresh_texture, entry.name,
                entry.parameter, reason);
        }
        if (entry.kind == entry_kind_sound) {
            const auto args = std::make_format_args(entry.name);
            const auto reason = rv_editor_text_format("change.sound_restart_required", args);
            return rv_editor_change_plan_of(rv_editor_change_action::restart_required, entry.name, "", reason);
        }
        // kind == "file"
        return rv_editor_change_plan_of(rv_editor_change_action::restart_required, entry.name, "",
            rv_editor_text("change.file_reload_not_supported"));
    }

    if (rv_editor_change_is_cpp(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "",
            rv_editor_text("change.save_and_rebuild"));
    }
    return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "",
        rv_editor_text("change.not_on_disc"));
}

} // namespace rv_editor
