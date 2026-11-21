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
// Scene preset: left column (Scene over Assets and Inspector) share of the width.
constexpr float scene_left_ratio = 0.58f;
// Scene preset: Game share of the right column below its strip row; Hierarchy takes the rest.
constexpr float scene_game_ratio = 0.50f;
// Scene preset: Scene share of the left column's height; Assets and Inspector take the rest.
constexpr float scene_scene_ratio = 0.55f;
// Scene preset: Assets share of the row under the Scene, Inspector takes the rest.
constexpr float scene_assets_ratio = 0.50f;
// Debug preset: game and session share above the runtime log.
constexpr float debug_log_ratio = 0.71f;
// Debug preset: game share beside the session column.
constexpr float debug_column_ratio = 0.68f;
// Burn preset: work area (candidate and release column) share of the height; the build log strip takes the rest.
constexpr float burn_log_ratio = 0.60f;

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
    b.add(rv_editor_pane_kind::inspector_report, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::terminal, output, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::search, output, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, output);
}

// Two columns, so the tree's minimum fits 1280 px: on the left the Scene over Assets (Assets, Files and Inspector
// Report tabs) beside Inspector; on the right a column of the Toolchest, Runtime Controls, the Game and
// Hierarchy down to the bottom.
void rv_editor_preset_scene(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id scene = 0;
    const rv_editor_pane_id game = b.add(rv_editor_pane_kind::game, scene, rv_editor_tile_dock::right, scene_left_ratio);
    b.add(rv_editor_pane_kind::hierarchy, game, rv_editor_tile_dock::bottom, scene_game_ratio);
    // The controls are a strip as tall as their buttons: the game takes the rest.
    const rv_editor_pane_id controls = b.add(rv_editor_pane_kind::controls, game, rv_editor_tile_dock::top, 1.0f);
    b.add(rv_editor_pane_kind::toolchest, controls, rv_editor_tile_dock::top, 1.0f);
    const rv_editor_pane_id assets = b.add(rv_editor_pane_kind::assets, scene, rv_editor_tile_dock::bottom, scene_scene_ratio);
    b.add(rv_editor_pane_kind::inspector, assets, rv_editor_tile_dock::right, scene_assets_ratio);
    b.add(rv_editor_pane_kind::files, assets, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::inspector_report, assets, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, assets);
}

// Debug: the Game column (Runtime Controls strip over the Game) beside Session, Observe
// and Test Case, the Runtime Log, Findings and Toolchest along the bottom. Code, Files
// and the Terminal open from Window > New Tile.
void rv_editor_preset_debug(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id controls = 0;
    const rv_editor_pane_id log =
        b.add(rv_editor_pane_kind::runtime_log, controls, rv_editor_tile_dock::bottom, debug_log_ratio);
    const rv_editor_pane_id session =
        b.add(rv_editor_pane_kind::session, controls, rv_editor_tile_dock::right, debug_column_ratio);
    // The controls are a strip as tall as their buttons: the game takes the rest.
    b.add(rv_editor_pane_kind::game, controls, rv_editor_tile_dock::bottom, 1.0f);
    b.add(rv_editor_pane_kind::observe, session, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::test_case, session, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::findings, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::inspector_report, log, rv_editor_tile_dock::tab, 0.0f);
    // A tab, not a strip: the layout tests fix the log leaf.
    b.add(rv_editor_pane_kind::toolchest, log, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, session);
    (void)rv_editor_tile_activate(b.layout, log);
}

// Burn: the candidate (Export beside it, Candidate in front) and the release controls
// over the Game, in one row on top; the build log (Runtime Log beside it, Build Log in
// front) as a strip across the full width at the bottom.
void rv_editor_preset_burn(rv_editor_preset_builder &b)
{
    const rv_editor_pane_id candidate = 0;
    const rv_editor_pane_id log = b.add(rv_editor_pane_kind::build_log, candidate, rv_editor_tile_dock::bottom, burn_log_ratio);
    // The release column is as wide as its controls and the Game need; the candidate takes the rest.
    const rv_editor_pane_id release = b.add(rv_editor_pane_kind::release_controls, candidate, rv_editor_tile_dock::right, 1.0f);
    // The release controls are a strip as tall as their buttons: the game takes the rest.
    b.add(rv_editor_pane_kind::game, release, rv_editor_tile_dock::bottom, 0.0f);
    b.add(rv_editor_pane_kind::export_, candidate, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::runtime_log, log, rv_editor_tile_dock::tab, 0.0f);
    b.add(rv_editor_pane_kind::inspector_report, log, rv_editor_tile_dock::tab, 0.0f);
    (void)rv_editor_tile_activate(b.layout, candidate);
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
        first = rv_editor_pane_kind::candidate;
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
