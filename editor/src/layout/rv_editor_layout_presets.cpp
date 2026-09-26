// Starting layouts: the working default, Test and Release, and the editor's design
// references Code and Scene.

#include "layout/rv_editor_tile.hpp"

namespace rv_editor
{

namespace
{

// Builds a tree by inserting panes next to panes already placed.
struct rv_editor_preset_builder
{
    rv_editor_pane_registry panes{};
    rv_editor_layout layout{};

    explicit rv_editor_preset_builder(rv_editor_pane_kind first)
    {
        layout = rv_editor_layout_make(rv_editor_pane_add(panes, first));
    }

    // Places a new pane of `kind` beside the leaf holding `next_to`; `ratio` is the
    // first child's share of the split this creates. Returns the new pane.
    rv_editor_pane_id add(rv_editor_pane_kind kind, rv_editor_pane_id next_to, rv_editor_tile_dock dock, float ratio)
    {
        const rv_editor_pane_id pane = rv_editor_pane_add(panes, kind);
        const uint32_t leaf = rv_editor_tile_insert(layout, rv_editor_tile_find(layout, next_to), pane, dock);
        if (dock != rv_editor_tile_dock::tab) {
            rv_editor_tile_set_ratio(layout, layout.nodes[leaf].parent, ratio);
        }
        return pane;
    }
};

// Reference 0003, without its Toolchest: files on the left, the code beside the
// game over its controls, the terminal and the runtime output underneath.
// Ratios are the picture's, measured in pixels.
void rv_editor_preset_code(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.16f);
    const rv_editor_pane_id terminal = b.add(rv_editor_pane_kind::terminal, code, rv_editor_tile_dock::bottom, 0.72f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.54f);
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 0.74f);
    b.add(rv_editor_pane_kind::output, terminal, rv_editor_tile_dock::right, 0.54f);
}

// Reference 0005: tools over the hierarchy, the scene over the assets, the game
// over the inspector over the console.
void rv_editor_preset_scene(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id scene = 0;
    const rv_editor_pane_id hierarchy = b.add(rv_editor_pane_kind::hierarchy, scene, rv_editor_tile_dock::left, 0.16f);
    b.add(rv_editor_pane_kind::toolchest, hierarchy, rv_editor_tile_dock::top, 0.29f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, scene, rv_editor_tile_dock::right, 0.61f);
    const rv_editor_pane_id assets = b.add(rv_editor_pane_kind::assets, scene, rv_editor_tile_dock::bottom, 0.70f);
    b.add(rv_editor_pane_kind::files, assets, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, assets);
    const rv_editor_pane_id inspector = b.add(rv_editor_pane_kind::inspector, game, rv_editor_tile_dock::bottom, 0.42f);
    b.add(rv_editor_pane_kind::console, inspector, rv_editor_tile_dock::bottom, 0.62f);
}

// Test (reference 0008): a strip of runtime controls as tall as its content over
// the game beside Observe, the source and the files, the session's log underneath. The
// ratios are the requirements' (72/28, 62/38); the strip's is its minimum.
void rv_editor_preset_test(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::runtime_log, game, rv_editor_tile_dock::bottom, 0.72f);
    const rv_editor_pane_id observe = b.add(rv_editor_pane_kind::observe, game, rv_editor_tile_dock::right, 0.62f);
    b.add(rv_editor_pane_kind::code, observe, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::files, observe, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, observe);
    b.add(rv_editor_pane_kind::terminal, log, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, log);
}

// Release (reference 0007): the release controls over the build's log beside
// the playtest, the runtime's log as a tab under it (72/28, 35/65).
void rv_editor_preset_release(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id build = b.add(rv_editor_pane_kind::build_log, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::runtime_log, build, rv_editor_tile_dock::bottom, 0.72f);
    b.add(rv_editor_pane_kind::game, build, rv_editor_tile_dock::right, 0.35f);
    rv_editor_tile_activate(b.layout, log);
}

// The working default, of panes that exist: files on the left, code in the middle
// over its output and a terminal as tabs, the game with compact runtime controls
// on the right.
void rv_editor_preset_workspace(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.18f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.60f);
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 0.80f);
    const rv_editor_pane_id output = b.add(rv_editor_pane_kind::output, code, rv_editor_tile_dock::bottom, 0.70f);
    b.add(rv_editor_pane_kind::terminal, output, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, output);
}

} // namespace

const char *rv_editor_layout_preset_name(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::code:
        return "Code";
    case rv_editor_layout_preset::scene:
        return "Scene";
    case rv_editor_layout_preset::test:
        return "Test";
    case rv_editor_layout_preset::release:
        return "Release";
    case rv_editor_layout_preset::workspace:
        return "Default";
    }
    return "";
}

void rv_editor_layout_preset_make(rv_editor_layout_preset preset, rv_editor_pane_registry &panes,
    rv_editor_layout &layout)
{
    rv_editor_pane_kind first = rv_editor_pane_kind::code;
    if (preset == rv_editor_layout_preset::scene) {
        first = rv_editor_pane_kind::scene;
    } else if (preset == rv_editor_layout_preset::test || preset == rv_editor_layout_preset::release) {
        first = rv_editor_pane_kind::controls;
    }
    rv_editor_preset_builder b(first);
    switch (preset) {
    case rv_editor_layout_preset::code:
        rv_editor_preset_code(b);
        break;
    case rv_editor_layout_preset::scene:
        rv_editor_preset_scene(b);
        break;
    case rv_editor_layout_preset::test:
        rv_editor_preset_test(b);
        break;
    case rv_editor_layout_preset::release:
        rv_editor_preset_release(b);
        break;
    case rv_editor_layout_preset::workspace:
        rv_editor_preset_workspace(b);
        break;
    }
    panes = b.panes;
    layout = b.layout;
}

} // namespace rv_editor
