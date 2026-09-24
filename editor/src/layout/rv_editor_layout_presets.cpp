// Starting layouts (requirements LAY-07): Scene, Code + Game, Debug/Output.

#include "layout/rv_editor_tile.hpp"

namespace rv_editor
{
const char *rv_editor_layout_preset_name(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::scene:
        return "Scene";
    case rv_editor_layout_preset::code_game:
        return "Code + Game";
    case rv_editor_layout_preset::debug_output:
        return "Debug/Output";
    }
    return "";
}

void rv_editor_layout_preset_make(rv_editor_layout_preset preset, rv_editor_pane_registry &panes,
    rv_editor_layout &layout)
{
    if (preset == rv_editor_layout_preset::scene) {
        rv_editor_pane_registry reg{};
        rv_editor_pane_id scene = rv_editor_pane_add(reg, rv_editor_pane_kind::scene);
        rv_editor_pane_id game = rv_editor_pane_add(reg, rv_editor_pane_kind::game);
        rv_editor_pane_id hierarchy = rv_editor_pane_add(reg, rv_editor_pane_kind::hierarchy);
        rv_editor_pane_id files = rv_editor_pane_add(reg, rv_editor_pane_kind::files);
        rv_editor_pane_id inspector = rv_editor_pane_add(reg, rv_editor_pane_kind::inspector);
        rv_editor_pane_id assets = rv_editor_pane_add(reg, rv_editor_pane_kind::assets);
        rv_editor_pane_id output = rv_editor_pane_add(reg, rv_editor_pane_kind::output);
        rv_editor_pane_id problems = rv_editor_pane_add(reg, rv_editor_pane_kind::problems);

        rv_editor_layout lay = rv_editor_layout_make(scene);
        uint32_t scene_leaf = rv_editor_tile_find(lay, scene);
        uint32_t hierarchy_leaf = rv_editor_tile_insert(lay, scene_leaf, hierarchy, rv_editor_tile_dock::left);
        rv_editor_tile_set_ratio(lay, lay.nodes[hierarchy_leaf].parent, 0.20f);
        rv_editor_tile_insert(lay, hierarchy_leaf, files, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, hierarchy);
        uint32_t inspector_leaf = rv_editor_tile_insert(lay, scene_leaf, inspector, rv_editor_tile_dock::right);
        rv_editor_tile_set_ratio(lay, lay.nodes[inspector_leaf].parent, 0.75f);
        uint32_t assets_leaf = rv_editor_tile_insert(lay, scene_leaf, assets, rv_editor_tile_dock::bottom);
        rv_editor_tile_set_ratio(lay, lay.nodes[assets_leaf].parent, 0.70f);
        rv_editor_tile_insert(lay, assets_leaf, output, rv_editor_tile_dock::tab);
        rv_editor_tile_insert(lay, assets_leaf, problems, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, assets);
        rv_editor_tile_insert(lay, scene_leaf, game, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, scene);
        panes = reg;
        layout = lay;
    } else if (preset == rv_editor_layout_preset::code_game) {
        rv_editor_pane_registry reg{};
        rv_editor_pane_id code = rv_editor_pane_add(reg, rv_editor_pane_kind::code);
        rv_editor_pane_id files = rv_editor_pane_add(reg, rv_editor_pane_kind::files);
        rv_editor_pane_id project = rv_editor_pane_add(reg, rv_editor_pane_kind::project);
        rv_editor_pane_id game = rv_editor_pane_add(reg, rv_editor_pane_kind::game);
        rv_editor_pane_id controls = rv_editor_pane_add(reg, rv_editor_pane_kind::controls);
        rv_editor_pane_id output = rv_editor_pane_add(reg, rv_editor_pane_kind::output);
        rv_editor_pane_id problems = rv_editor_pane_add(reg, rv_editor_pane_kind::problems);
        rv_editor_pane_id terminal = rv_editor_pane_add(reg, rv_editor_pane_kind::terminal);

        rv_editor_layout lay = rv_editor_layout_make(code);
        uint32_t code_leaf = rv_editor_tile_find(lay, code);
        uint32_t files_leaf = rv_editor_tile_insert(lay, code_leaf, files, rv_editor_tile_dock::left);
        rv_editor_tile_set_ratio(lay, lay.nodes[files_leaf].parent, 0.18f);
        rv_editor_tile_insert(lay, files_leaf, project, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, files);
        uint32_t game_leaf = rv_editor_tile_insert(lay, code_leaf, game, rv_editor_tile_dock::right);
        rv_editor_tile_set_ratio(lay, lay.nodes[game_leaf].parent, 0.60f);
        uint32_t controls_leaf = rv_editor_tile_insert(lay, game_leaf, controls, rv_editor_tile_dock::bottom);
        rv_editor_tile_set_ratio(lay, lay.nodes[controls_leaf].parent, 0.60f);
        uint32_t output_leaf = rv_editor_tile_insert(lay, code_leaf, output, rv_editor_tile_dock::bottom);
        rv_editor_tile_set_ratio(lay, lay.nodes[output_leaf].parent, 0.72f);
        rv_editor_tile_insert(lay, output_leaf, problems, rv_editor_tile_dock::tab);
        rv_editor_tile_insert(lay, output_leaf, terminal, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, output);
        panes = reg;
        layout = lay;
    } else if (preset == rv_editor_layout_preset::debug_output) {
        rv_editor_pane_registry reg{};
        rv_editor_pane_id game = rv_editor_pane_add(reg, rv_editor_pane_kind::game);
        rv_editor_pane_id controls = rv_editor_pane_add(reg, rv_editor_pane_kind::controls);
        rv_editor_pane_id inspector = rv_editor_pane_add(reg, rv_editor_pane_kind::inspector);
        rv_editor_pane_id run_config = rv_editor_pane_add(reg, rv_editor_pane_kind::run_config);
        rv_editor_pane_id output = rv_editor_pane_add(reg, rv_editor_pane_kind::output);
        rv_editor_pane_id terminal = rv_editor_pane_add(reg, rv_editor_pane_kind::terminal);
        rv_editor_pane_id problems = rv_editor_pane_add(reg, rv_editor_pane_kind::problems);

        rv_editor_layout lay = rv_editor_layout_make(game);
        uint32_t game_leaf = rv_editor_tile_find(lay, game);
        uint32_t controls_leaf = rv_editor_tile_insert(lay, game_leaf, controls, rv_editor_tile_dock::right);
        rv_editor_tile_set_ratio(lay, lay.nodes[controls_leaf].parent, 0.55f);
        uint32_t inspector_leaf = rv_editor_tile_insert(lay, controls_leaf, inspector,
            rv_editor_tile_dock::bottom);
        rv_editor_tile_set_ratio(lay, lay.nodes[inspector_leaf].parent, 0.40f);
        rv_editor_tile_insert(lay, controls_leaf, run_config, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, controls);
        uint32_t output_leaf = rv_editor_tile_insert(lay, game_leaf, output, rv_editor_tile_dock::bottom);
        rv_editor_tile_set_ratio(lay, lay.nodes[output_leaf].parent, 0.55f);
        rv_editor_tile_insert(lay, output_leaf, terminal, rv_editor_tile_dock::tab);
        rv_editor_tile_insert(lay, output_leaf, problems, rv_editor_tile_dock::tab);
        rv_editor_tile_activate(lay, output);
        panes = reg;
        layout = lay;
    }
}

} // namespace rv_editor
