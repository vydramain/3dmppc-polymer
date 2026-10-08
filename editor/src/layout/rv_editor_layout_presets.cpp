// Starting layouts of Code, Scene, Debug and Burn: what each shows the first
// time it is chosen and after Layout > Reset Layout.

#include "layout/rv_editor_tile.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Code preset: left sidebar ratio.
constexpr float code_sidebar_ratio = 0.16f;
// Code preset: lower output panel ratio.
constexpr float code_output_ratio = 0.60f;
// Code preset: right game panel ratio.
constexpr float code_game_ratio = 0.60f;
// Scene preset: left sidebar ratio.
constexpr float scene_sidebar_ratio = 0.16f;
// Scene preset: top toolchest ratio above hierarchy.
constexpr float scene_toolchest_ratio = 0.29f;
// Scene preset: right game panel ratio.
constexpr float scene_game_ratio = 0.62f;
// Scene preset: bottom inspector ratio below game.
constexpr float scene_inspector_ratio = 0.52f;
// Scene preset: center assets panel ratio.
constexpr float scene_assets_ratio = 0.55f;
// Debug preset: runtime log ratio at bottom.
constexpr float debug_log_ratio = 0.71f;
// Debug preset: right column (session and toolchest) ratio.
constexpr float debug_column_ratio = 0.68f;
// Burn preset: build log ratio at bottom.
constexpr float burn_log_ratio = 0.68f;
// Burn preset: right game panel ratio.
constexpr float burn_game_ratio = 0.35f;

// Builds a tree by inserting panes next to panes already placed.
struct rv_editor_preset_builder {
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
            (void)rv_editor_tile_set_ratio(layout, layout.nodes[leaf].parent, ratio);
        }
        return pane;
    }
};

// Reference 0003, without its Toolchest: files on the left, the code beside the
// game over its controls, and under both one tile of Output, Problems, Terminal
// and Search, Output in front. Ratios are the picture's, measured in pixels, but
// the game is narrower: the Game's fit sets the height above Output from its width.
void rv_editor_preset_code(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id code = 0;
    b.add(rv_editor_pane_kind::files, code, rv_editor_tile_dock::left, code_sidebar_ratio);
    const rv_editor_pane_id output = b.add(rv_editor_pane_kind::output, code, rv_editor_tile_dock::bottom, code_output_ratio);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, code, rv_editor_tile_dock::right, code_game_ratio);
    // The controls are a strip as tall as their buttons: the game takes the rest.
    b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::bottom, 1.0f);
    b.add(rv_editor_pane_kind::problems, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::terminal, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::search, output, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, output);
}

// Toolchest over Hierarchy down the left and Game over Inspector down the right,
// both full height so the Inspector keeps its rows at 1280x720; between them the
// Scene over a tile of Assets, Files and Console Output (Assets in front) above
// Runtime Controls.
void rv_editor_preset_scene(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id scene = 0;
    const rv_editor_pane_id hierarchy =
        b.add(rv_editor_pane_kind::hierarchy, scene, rv_editor_tile_dock::left, scene_sidebar_ratio);
    b.add(rv_editor_pane_kind::toolchest, hierarchy, rv_editor_tile_dock::top, scene_toolchest_ratio);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, scene, rv_editor_tile_dock::right, scene_game_ratio);
    b.add(rv_editor_pane_kind::inspector, game, rv_editor_tile_dock::bottom, scene_inspector_ratio);
    const rv_editor_pane_id assets = b.add(rv_editor_pane_kind::assets, scene, rv_editor_tile_dock::bottom, scene_assets_ratio);
    b.add(rv_editor_pane_kind::files, assets, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::output, assets, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, assets);
    // The controls are a strip as tall as their buttons: the assets take the rest.
    b.add(rv_editor_pane_kind::controls, assets, rv_editor_tile_dock::bottom, 1.0f);
}

// Debug: the session in front. Runtime Controls beside the Session Toolchest
// along the top (both strips, fit to the taller); below them the Game beside
// Session, Observe and Test Case; the Runtime Log and Findings along the
// bottom; Code, Files and the Terminal open from Window > New Tile.
void rv_editor_preset_debug(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id log =
        b.add(rv_editor_pane_kind::runtime_log, controls, rv_editor_tile_dock::bottom, debug_log_ratio);
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id session = b.add(rv_editor_pane_kind::session, game, rv_editor_tile_dock::right, debug_column_ratio);
    b.add(rv_editor_pane_kind::observe, session, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::test_case, session, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::findings, log, rv_editor_tile_dock::tab, 0.0f);
    // Toolchest aligned with the Session column below it: same 0.68 split.
    b.add(rv_editor_pane_kind::toolchest, controls, rv_editor_tile_dock::right, debug_column_ratio);
    (void)rv_editor_tile_activate(b.layout, session);
    (void)rv_editor_tile_activate(b.layout, log);
}

// Burn (reference 0007): the release controls over the candidate beside its
// playtest, the build's and the playtest's logs underneath (68/32, 35/65).
void rv_editor_preset_burn(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id candidate = b.add(rv_editor_pane_kind::candidate, controls, rv_editor_tile_dock::bottom, 0.0f);
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::build_log, candidate, rv_editor_tile_dock::bottom, burn_log_ratio);
    b.add(rv_editor_pane_kind::game, candidate, rv_editor_tile_dock::right, burn_game_ratio);
    b.add(rv_editor_pane_kind::runtime_log, log, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, log);
}

} // namespace

const char *rv_editor_layout_preset_name(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::code:
        return rv_editor_text("layout_presets.code");
    case rv_editor_layout_preset::scene:
        return rv_editor_text("layout_presets.scene");
    case rv_editor_layout_preset::debug:
        return rv_editor_text("layout_presets.debug");
    case rv_editor_layout_preset::burn:
        return rv_editor_text("layout_presets.burn");
    }
    return "";
}

void rv_editor_layout_preset_make(rv_editor_layout_preset preset, rv_editor_pane_registry &panes, rv_editor_layout &layout)
{
    rv_editor_pane_kind first = rv_editor_pane_kind::code;
    if (preset == rv_editor_layout_preset::scene) {
        first = rv_editor_pane_kind::scene;
    } else if (preset == rv_editor_layout_preset::burn) {
        first = rv_editor_pane_kind::release_controls;
    } else if (preset == rv_editor_layout_preset::debug) {
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
