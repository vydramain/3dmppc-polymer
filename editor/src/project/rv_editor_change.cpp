// One decision for "this project file changed: what does the running game
// need?" -- see README's "What still needs a restart" for the contract.

#include "project/rv_editor_change.hpp"

#include <string_view>

namespace rv_editor
{

namespace
{

bool rv_editor_change_is_cpp_source(std::string_view rel)
{
    static constexpr std::string_view exts[] = { ".cpp", ".cc", ".c", ".hpp", ".h" };
    for (std::string_view ext : exts) {
        if (rel.ends_with(ext)) {
            return true;
        }
    }
    return false;
}

rv_editor_change_result rv_editor_change_from_map_entry(const rv_editor_map_entry &e)
{
    if (e.kind == "entry") {
        return {
            rv_editor_change_action::reload_entry, e.name, {},
            "The entry script changed: Reload applies it without a restart."
        };
    }
    if (e.kind == "module") {
        return {
            rv_editor_change_action::reload_module, e.parameter, {},
            "Lua module " + e.parameter + " changed: Reload applies it without a restart."
        };
    }
    if (e.kind == "texture") {
        return {
            rv_editor_change_action::refresh_texture, e.name, e.parameter,
            "Texture " + e.name + " changed: Reload bakes it and refreshes it without a restart."
        };
    }
    if (e.kind == "sound") {
        return {
            rv_editor_change_action::restart_required, e.name, {},
            "A resident sound changes only across a restart: Build and Restart."
        };
    }
    if (e.kind == "code") {
        return {
            rv_editor_change_action::build_restart, e.name, {},
            "A C++ source changed: Build and Restart."
        };
    }
    // kind "file", or anything else the map names that is none of the above.
    return {
        rv_editor_change_action::restart_required, e.name, {},
        "The console cannot reload this kind of file: Build and Restart."
    };
}

} // namespace

rv_editor_change_result rv_editor_change_classify(
    const std::filesystem::path &root, const std::filesystem::path &manifest,
    const std::map<std::string, rv_editor_map_entry> &map, const std::filesystem::path &changed)
{
    if (map.empty()) {
        return { rv_editor_change_action::none, {}, {}, "Nothing is running, so there is nothing to apply." };
    }

    std::error_code ec;
    const std::string rel = std::filesystem::relative(changed, root, ec).generic_string();
    const std::string rel_manifest = std::filesystem::relative(manifest, root, ec).generic_string();

    if (rel == rel_manifest) {
        return { rv_editor_change_action::build_restart, {}, {}, "disc.toml changed: Build and Restart." };
    }
    if (rel.ends_with(".scene.toml")) {
        return {
            rv_editor_change_action::build_restart, {}, {},
            "A scene document changed: scenes are not reloaded yet -- Save, then Build and Restart."
        };
    }

    const auto it = map.find(rel);
    if (it != map.end()) {
        return rv_editor_change_from_map_entry(it->second);
    }

    if (rv_editor_change_is_cpp_source(rel)) {
        return { rv_editor_change_action::build_restart, {}, {}, "A C++ source changed: Build and Restart." };
    }

    return {
        rv_editor_change_action::not_in_disc, {}, {},
        "This file is not on the running disc; Build and Restart puts it there if disc.toml lists it."
    };
}

} // namespace rv_editor
