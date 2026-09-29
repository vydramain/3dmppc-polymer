#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "build/rv_editor_build_map.hpp"

namespace rv_editor
{

// What a changed file means for the running console, per README's "What
// still needs a restart".
enum class rv_editor_change_action
{
    none,             // nothing runs
    reload_entry,     // Reload applies the entry chunk without a restart
    reload_module,    // Reload applies a required module without a restart
    refresh_texture,  // Reload bakes and refreshes a texture without a restart
    build_restart,    // Build and Restart: a source, disc.toml or a scene
    restart_required, // a resident sound, or a file kind with no reload
    not_in_disc,      // not in the running build's map at all
};

struct rv_editor_change_result
{
    rv_editor_change_action action = rv_editor_change_action::none;
    std::string name;      // the disc entry or module name; empty when moot
    std::string parameter; // a texture's format; empty otherwise
    std::string reason;    // one plain-English sentence for the UI
};

// What `changed` (an absolute path) means for the session running `map`
// (the build map of the artifact it was started from; empty when nothing
// runs). `root` is the project root, `manifest` its disc.toml.
rv_editor_change_result rv_editor_change_classify(
    const std::filesystem::path &root, const std::filesystem::path &manifest,
    const std::map<std::string, rv_editor_map_entry> &map, const std::filesystem::path &changed);

} // namespace rv_editor
