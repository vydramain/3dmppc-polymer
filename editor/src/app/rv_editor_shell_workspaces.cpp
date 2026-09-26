// The named workspaces: switching, resetting, and their layout files.

#include "app/rv_editor_shell.hpp"

#include <cstdio>
#include <string>
#include <system_error>

namespace rv_editor
{

namespace
{

size_t rv_editor_workspace_slot(rv_editor_layout_preset preset)
{
    for (size_t i = 0; i < std::size(rv_editor_workspaces); ++i) {
        if (rv_editor_workspaces[i] == preset) {
            return i;
        }
    }
    return 0;
}

// "default", "test", "release": the view file's names and the layout files' suffixes.
const char *rv_editor_workspace_key_of(rv_editor_layout_preset preset)
{
    switch (preset) {
        case rv_editor_layout_preset::test: return "test";
        case rv_editor_layout_preset::release: return "release";
        default: return "default";
    }
}

// The Default workspace keeps the file earlier editors wrote, so a layout made
// before workspaces existed stays the user's.
std::filesystem::path rv_editor_workspace_file(const std::filesystem::path &path, rv_editor_layout_preset preset)
{
    if (preset == rv_editor_layout_preset::workspace) {
        return path;
    }
    return path.parent_path() / (path.filename().string() + "-" + rv_editor_workspace_key_of(preset));
}

// `preset`'s starting tree, its panes joined to `panes`.
rv_editor_layout rv_editor_workspace_start(rv_editor_pane_registry &panes, rv_editor_layout_preset preset)
{
    rv_editor_pane_registry own;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(preset, own, layout);
    rv_editor_pane_adopt(panes, own, layout);
    return layout;
}

} // namespace

void rv_editor_shell_switch(rv_editor_shell &shell, rv_editor_layout_preset to)
{
    if (to == shell.active) {
        return;
    }
    shell.trees[rv_editor_workspace_slot(shell.active)] = std::move(shell.ws.layout);
    shell.ws.layout = std::move(shell.trees[rv_editor_workspace_slot(to)]);
    shell.active = to;
    shell.ws.focused_leaf = rv_editor_tile_none;
}

void rv_editor_shell_reset_layout(rv_editor_shell &shell, rv_editor_layout_preset preset)
{
    shell.ws.layout = rv_editor_workspace_start(shell.ws.panes, preset);
    shell.ws.focused_leaf = rv_editor_tile_none;
}

void rv_editor_shell_load_layouts(rv_editor_shell &shell, const std::filesystem::path &path, const std::string &active)
{
    shell.ws.panes = {};
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        rv_editor_layout &tree = shell.trees[rv_editor_workspace_slot(preset)];
        rv_editor_pane_registry own;
        rv_editor_layout layout;
        const std::filesystem::path file = path.empty() ? path : rv_editor_workspace_file(path, preset);
        if (!file.empty() && rv_editor_layout_load(file, own, layout)) {
            rv_editor_pane_adopt(shell.ws.panes, own, layout);
            tree = std::move(layout);
            continue;
        }
        std::error_code ec;
        if (!file.empty() && std::filesystem::exists(file, ec)) {
            std::fprintf(stderr, "3dmppc-editor: %s is not a layout this editor reads; starting from %s\n",
                file.c_str(), rv_editor_layout_preset_name(preset));
        }
        tree = rv_editor_workspace_start(shell.ws.panes, preset);
    }
    shell.active = rv_editor_layout_preset::workspace;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (active == rv_editor_workspace_key_of(preset)) {
            shell.active = preset;
        }
    }
    shell.ws.layout = std::move(shell.trees[rv_editor_workspace_slot(shell.active)]);
    shell.ws.focused_leaf = rv_editor_tile_none;
}

bool rv_editor_shell_save_layouts(const rv_editor_shell &shell, const std::filesystem::path &path, std::string &error)
{
    bool ok = true;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        const rv_editor_layout &tree =
            preset == shell.active ? shell.ws.layout : shell.trees[rv_editor_workspace_slot(preset)];
        std::string why;
        if (!rv_editor_layout_save(rv_editor_workspace_file(path, preset), shell.ws.panes, tree, why) && ok) {
            error = why;
            ok = false;
        }
    }
    return ok;
}

const char *rv_editor_shell_workspace_key(const rv_editor_shell &shell)
{
    return rv_editor_workspace_key_of(shell.active);
}

bool rv_editor_shell_pane_kept(const rv_editor_shell &shell, rv_editor_pane_id pane)
{
    if (rv_editor_tile_find(shell.ws.layout, pane) != rv_editor_tile_none) {
        return true;
    }
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (preset != shell.active &&
            rv_editor_tile_find(shell.trees[rv_editor_workspace_slot(preset)], pane) != rv_editor_tile_none) {
            return true;
        }
    }
    return false;
}

} // namespace rv_editor
