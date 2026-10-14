#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "build/rv_editor_build_map.hpp"

namespace rv_editor
{

// What applying a changed file does to the running session.
enum class rv_editor_change_action {
    reload_entry,     // map kind entry
    reload_module,    // map kind module
    refresh_texture,  // map kind texture
    build_restart,    // a C++ source, disc.toml, or a scene: Build and Restart
    restart_required, // map kind sound or file: only a restart picks it up
    not_in_disc,      // not in the map at all
    none,             // the map is empty: nothing runs
};

// name/parameter are the map's, per action: the entry or module name, the
// texture's disc name and format. reason is one plain sentence for the user.
struct rv_editor_change_plan {
    rv_editor_change_action action = rv_editor_change_action::none;
    std::string name;
    std::string parameter;
    std::string reason;
};

// Pure: no UI, no process, no app state. `map` is the running build's map
// (empty when nothing runs); `changed` is the file that just changed, as an
// absolute path.
rv_editor_change_plan rv_editor_change_plan_for(const std::filesystem::path &root,
    const std::filesystem::path &manifest,
    const std::map<std::string, rv_editor_map_entry> &map,
    const std::filesystem::path &changed);

} // namespace rv_editor
