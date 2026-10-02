// Decides what a changed file does to the running session, from the burner's
// map alone: no filesystem access beyond lexical path comparison.

#include "project/rv_editor_change.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace rv_editor
{

namespace
{

bool rv_editor_change_is_cpp(const std::filesystem::path &path)
{
    static const std::array<std::string, 5> exts = { ".c", ".cc", ".cpp", ".h", ".hpp" };
    const std::string ext = path.extension().string();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

bool rv_editor_change_is_scene(const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    const std::string suffix = ".scene.toml";
    if (name.size() < suffix.size()) {
        return false;
    }
    return name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
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
            "Nothing is running, so there is nothing to apply.");
    }

    const std::filesystem::path changed_norm = changed.lexically_normal();

    if (changed_norm == manifest.lexically_normal()) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "", "Save, then Build and Restart.");
    }
    if (rv_editor_change_is_scene(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "",
            "Scenes are not reloaded yet. Save, then Build and Restart.");
    }

    // Lexical, not resolved: root and changed must be spelled the same way, or
    // a symlinked root against an already-resolved path reads as outside it.
    const std::filesystem::path rel = changed_norm.lexically_relative(root.lexically_normal());
    const bool outside_root = rel.empty() || rel.begin()->string() == "..";
    if (outside_root) {
        return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "", "Not part of this project.");
    }

    const auto found = map.find(rel.generic_string());
    if (found != map.end()) {
        const rv_editor_map_entry &entry = found->second;
        if (entry.kind == "code") {
            return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "", "Save, then Build and Restart.");
        }
        if (entry.kind == "entry") {
            return rv_editor_change_plan_of(rv_editor_change_action::reload_entry, entry.name, "",
                "Reload the entry script \"" + entry.name + "\".");
        }
        if (entry.kind == "module") {
            return rv_editor_change_plan_of(rv_editor_change_action::reload_module, entry.parameter, "",
                "Reload module \"" + entry.parameter + "\".");
        }
        if (entry.kind == "texture") {
            return rv_editor_change_plan_of(rv_editor_change_action::refresh_texture, entry.name, entry.parameter,
                "Refresh texture \"" + entry.name + "\".");
        }
        if (entry.kind == "sound") {
            return rv_editor_change_plan_of(rv_editor_change_action::restart_required, entry.name, "",
                "Sound \"" + entry.name + "\" changes only across a restart: Build and Restart.");
        }
        // kind == "file"
        return rv_editor_change_plan_of(rv_editor_change_action::restart_required, entry.name, "",
            "The console cannot reload this kind of file: Build and Restart.");
    }

    if (rv_editor_change_is_cpp(changed_norm)) {
        return rv_editor_change_plan_of(rv_editor_change_action::build_restart, "", "", "Save, then Build and Restart.");
    }
    return rv_editor_change_plan_of(rv_editor_change_action::not_in_disc, "", "",
        "Not on the running disc. Build and Restart puts it there if disc.toml lists it.");
}

} // namespace rv_editor
