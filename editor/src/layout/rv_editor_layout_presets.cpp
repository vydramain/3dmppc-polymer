// Starting layouts of Code, Scene, Debug and Burn: what each shows the first
// time it is chosen and after Layout > Reset Layout.

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
    // The controls are a strip as tall as their buttons: the game takes the rest.
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 1.0f);
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

// Debug: files on the left, the code over the terminal beside the game over its
// controls over the runtime output. Ratios are the owner's screenshot, in pixels.
void rv_editor_preset_debug(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.16f);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.54f);
    b.add(rv_editor_pane_kind::terminal, code, rv_editor_tile_dock::bottom, 0.72f);
    const rv_editor_pane_id controls = b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 0.54f);
    b.add(rv_editor_pane_kind::output, controls, rv_editor_tile_dock::bottom, 0.0f);
}

// Burn (reference 0007): the release controls over the candidate beside its
// playtest, the build's and the playtest's logs underneath (72/28, 35/65).
void rv_editor_preset_burn(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id candidate =
        b.add(rv_editor_pane_kind::candidate, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::build_log, candidate, rv_editor_tile_dock::bottom, 0.72f);
    b.add(rv_editor_pane_kind::game, candidate, rv_editor_tile_dock::right, 0.35f);
    b.add(rv_editor_pane_kind::runtime_log, log, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, log);
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
    case rv_editor_layout_preset::burn:
        return "Burn";
    }
    return "";
}

void rv_editor_layout_preset_make(rv_editor_layout_preset preset, rv_editor_pane_registry &panes,
    rv_editor_layout &layout)
{
    rv_editor_pane_kind first = rv_editor_pane_kind::code;
    if (preset == rv_editor_layout_preset::scene) {
        first = rv_editor_pane_kind::scene;
    } else if (preset == rv_editor_layout_preset::burn) {
        first = rv_editor_pane_kind::release_controls;
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
    case rv_editor_layout_preset::burn:
        rv_editor_preset_burn(b);
        break;
    }
    panes = b.panes;
    layout = b.layout;
}

} // namespace rv_editor
