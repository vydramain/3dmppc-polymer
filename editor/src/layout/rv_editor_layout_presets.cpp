// Starting layouts: the working default, and the editor's design references Code,
// Scene, Debug and Build.

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

// Reference 0003: tools and files on the left, code with the game and its
// controls beside it, terminal and runtime output underneath.
void rv_editor_preset_code(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    const rv_editor_pane_id files = b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.16f);
    b.add(rv_editor_pane_kind::toolchest, files, rv_editor_tile_dock::top, 0.30f);
    const rv_editor_pane_id terminal = b.add(rv_editor_pane_kind::terminal, code, rv_editor_tile_dock::bottom, 0.70f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.60f);
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 0.60f);
    b.add(rv_editor_pane_kind::output, terminal, rv_editor_tile_dock::right, 0.60f);
}

// Reference 0005: tools over the hierarchy, the scene over the assets, the game
// over the inspector over the console.
void rv_editor_preset_scene(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id scene = 0;
    const rv_editor_pane_id hierarchy = b.add(rv_editor_pane_kind::hierarchy, scene, rv_editor_tile_dock::left, 0.16f);
    b.add(rv_editor_pane_kind::toolchest, hierarchy, rv_editor_tile_dock::top, 0.30f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, scene, rv_editor_tile_dock::right, 0.66f);
    const rv_editor_pane_id assets = b.add(rv_editor_pane_kind::assets, scene, rv_editor_tile_dock::bottom, 0.68f);
    b.add(rv_editor_pane_kind::files, assets, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, assets);
    const rv_editor_pane_id inspector = b.add(rv_editor_pane_kind::inspector, game, rv_editor_tile_dock::bottom, 0.35f);
    b.add(rv_editor_pane_kind::console, inspector, rv_editor_tile_dock::bottom, 0.55f);
}

// Reference 0002: game, controls and code on top; output, run configuration and
// inspector underneath.
void rv_editor_preset_debug(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id game = 0;
    const rv_editor_pane_id output = b.add(rv_editor_pane_kind::output, game, rv_editor_tile_dock::bottom, 0.62f);
    const rv_editor_pane_id controls = b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::right, 0.45f);
    b.add(rv_editor_pane_kind::code, controls, rv_editor_tile_dock::right, 0.35f);
    const rv_editor_pane_id run = b.add(rv_editor_pane_kind::run_config, output, rv_editor_tile_dock::right, 0.38f);
    b.add(rv_editor_pane_kind::inspector, run, rv_editor_tile_dock::right, 0.50f);
}

// Reference 0004: tools over the project, the code beside the controls over the
// game; build output, problems and the run configuration over a terminal below.
void rv_editor_preset_build(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    const rv_editor_pane_id project = b.add(rv_editor_pane_kind::project, code, rv_editor_tile_dock::left, 0.16f);
    b.add(rv_editor_pane_kind::toolchest, project, rv_editor_tile_dock::top, 0.30f);
    const rv_editor_pane_id output = b.add(rv_editor_pane_kind::output, code, rv_editor_tile_dock::bottom, 0.62f);
    const rv_editor_pane_id controls = b.add(rv_editor_pane_kind::controls, code, rv_editor_tile_dock::right, 0.60f);
    b.add(rv_editor_pane_kind::game, controls, rv_editor_tile_dock::bottom, 0.35f);
    b.add(rv_editor_pane_kind::search, output, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, output);
    const rv_editor_pane_id problems = b.add(rv_editor_pane_kind::problems, output, rv_editor_tile_dock::right, 0.40f);
    const rv_editor_pane_id run = b.add(rv_editor_pane_kind::run_config, problems, rv_editor_tile_dock::right, 0.50f);
    b.add(rv_editor_pane_kind::terminal, run, rv_editor_tile_dock::bottom, 0.55f);
}

// The working default, of panes that exist: files on the left, code in the middle
// over its output, the game with compact runtime controls on the right.
void rv_editor_preset_workspace(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.18f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.60f);
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 0.80f);
    b.add(rv_editor_pane_kind::output, code, rv_editor_tile_dock::bottom, 0.70f);
}

} // namespace

const char *rv_editor_layout_preset_name(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::code:
        return "Code";
    case rv_editor_layout_preset::scene:
        return "Scene";
    case rv_editor_layout_preset::debug:
        return "Debug";
    case rv_editor_layout_preset::build:
        return "Build";
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
    } else if (preset == rv_editor_layout_preset::debug) {
        first = rv_editor_pane_kind::game;
    }
    rv_editor_preset_builder b(first);
    switch (preset) {
    case rv_editor_layout_preset::code:
        rv_editor_preset_code(b);
        break;
    case rv_editor_layout_preset::scene:
        rv_editor_preset_scene(b);
        break;
    case rv_editor_layout_preset::debug:
        rv_editor_preset_debug(b);
        break;
    case rv_editor_layout_preset::build:
        rv_editor_preset_build(b);
        break;
    case rv_editor_layout_preset::workspace:
        rv_editor_preset_workspace(b);
        break;
    }
    panes = b.panes;
    layout = b.layout;
}

} // namespace rv_editor
