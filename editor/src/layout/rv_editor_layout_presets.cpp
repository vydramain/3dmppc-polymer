// Starting layouts of Code, Scene, Debug and Burn's two modes: what each shows
// the first time it is chosen and after Window > Reset Layout.

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

// Code (COD-01): the Toolchest over Files on the left (18 %); the code beside the
// game (62/38) over Output and a Terminal as tabs (74/26); the runtime controls
// under the game, as tall as they draw.
void rv_editor_preset_code(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    const rv_editor_pane_id files = b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, 0.18f);
    b.add(rv_editor_pane_kind::toolchest, files, rv_editor_tile_dock::top, 0.0f);
    const rv_editor_pane_id output = b.add(rv_editor_pane_kind::output, code, rv_editor_tile_dock::bottom, 0.74f);
    b.add(rv_editor_pane_kind::problems, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::terminal, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::search, output, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, output);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, 0.62f);
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 1.0f);
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

// Debug (DBG-01): the runtime controls as tall as they draw over the Toolchest
// with the session in brief, the game, the source and the runtime Inspector
// (12 %, then 44/32/24 of the rest); the runtime log, findings and a terminal
// underneath (72/28).
void rv_editor_preset_debug(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::runtime_log, game, rv_editor_tile_dock::bottom, 0.72f);
    b.add(rv_editor_pane_kind::findings, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::problems, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::terminal, log, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, log);
    b.add(rv_editor_pane_kind::toolchest, game, rv_editor_tile_dock::left, 0.12f);
    const rv_editor_pane_id code = b.add(rv_editor_pane_kind::code, game, rv_editor_tile_dock::right, 0.44f);
    b.add(rv_editor_pane_kind::observe, code, rv_editor_tile_dock::right, 0.57f);
}

// Burn, Candidate & Verify (BRN-03): the release controls over the Toolchest, the
// candidate and its playtest (12 %, then 35/65); the build's log, the playtest's
// log and findings underneath (72/28).
void rv_editor_preset_burn(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id candidate =
        b.add(rv_editor_pane_kind::candidate, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::build_log, candidate, rv_editor_tile_dock::bottom, 0.72f);
    b.add(rv_editor_pane_kind::checks, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::runtime_log, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::findings, log, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, log);
    b.add(rv_editor_pane_kind::toolchest, candidate, rv_editor_tile_dock::left, 0.12f);
    b.add(rv_editor_pane_kind::game, candidate, rv_editor_tile_dock::right, 0.35f);
}

// Burn, Build & Diagnose (BRN-02): the release controls over the Toolchest and
// Files (18 %) beside the build's result with the source as a tab; the build's
// log and a terminal underneath (70/30).
void rv_editor_preset_burn_diagnose(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id result =
        b.add(rv_editor_pane_kind::build_result, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id problems =
        b.add(rv_editor_pane_kind::problems, result, rv_editor_tile_dock::bottom, 0.70f);
    b.add(rv_editor_pane_kind::build_log, problems, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::search, problems, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::terminal, problems, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, problems);
    const rv_editor_pane_id files = b.add(rv_editor_pane_kind::files, result, rv_editor_tile_dock::left, 0.18f);
    b.add(rv_editor_pane_kind::toolchest, files, rv_editor_tile_dock::top, 0.0f);
    b.add(rv_editor_pane_kind::code, result, rv_editor_tile_dock::tab, 0.0f);
    rv_editor_tile_activate(b.layout, result);
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
    case rv_editor_layout_preset::burn_diagnose:
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
    } else if (preset != rv_editor_layout_preset::code) {
        first = preset == rv_editor_layout_preset::debug ? rv_editor_pane_kind::controls
                                                         : rv_editor_pane_kind::release_controls;
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
    case rv_editor_layout_preset::burn_diagnose:
        rv_editor_preset_burn_diagnose(b);
        break;
    }
    panes = b.panes;
    layout = b.layout;
}

} // namespace rv_editor
