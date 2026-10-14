#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_field_sizes.hpp"

namespace rv_editor
{

// A file browser drawn inside a page instead of a system dialog: what it is for,
// the path, Up, the entries, the one selected, the caller's action and Cancel.

enum class rv_editor_browse_pick {
    directory,  // directories only; the action takes the selected one, else the one listed
    executable, // directories and files; the action takes an executable file
};

enum class rv_editor_browse_result {
    none,
    picked,
    cancelled,
};

struct rv_editor_browser {
    std::string purpose; // "Select directory for New Project"
    std::string action;  // the button: "Select Directory", "Open Project"
    rv_editor_browse_pick pick = rv_editor_browse_pick::directory;
    std::filesystem::path dir;                  // the directory listed
    std::filesystem::path selected;             // an entry of dir; empty: none
    char path[filesystem_path_field_size] = {}; // the path field: follows dir, or typed
    // The entries of `listed`, directories first, read again when dir changes.
    struct rv_editor_entry {
        std::string name;
        bool dir = false;
    };
    std::filesystem::path listed;
    std::vector<rv_editor_entry> entries;
    std::string error; // why dir cannot be listed
};

// Starts at `start` (a file: its directory; missing: its nearest existing
// parent; empty: HOME) with nothing selected.
void rv_editor_browser_start(rv_editor_browser &b,
    std::string purpose,
    std::string action,
    rv_editor_browse_pick pick,
    const std::filesystem::path &start);

// What the action would take now: the selected entry, else (directory) the one listed.
std::filesystem::path rv_editor_browser_target(const rv_editor_browser &b);

// Draws it in `height` pixels (0: the rest of the region). `why_not` disables the
// action with its reason. picked puts the target in `out`; cancelled leaves it.
rv_editor_browse_result rv_editor_browser_draw(rv_editor_browser &b,
    float height,
    const char *why_not,
    std::filesystem::path &out,
    const rv_editor_theme &theme);

} // namespace rv_editor
