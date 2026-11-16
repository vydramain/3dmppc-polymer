// Smoke test of the starting layouts: each preset must leave a tree with at least one pane in it.

#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_layout_presets_not_empty)
{
    const rv_editor_layout_preset presets[] = {rv_editor_layout_preset::code, rv_editor_layout_preset::scene,
        rv_editor_layout_preset::debug, rv_editor_layout_preset::burn};
    for (rv_editor_layout_preset preset : presets) {
        rv_editor_pane_registry panes;
        rv_editor_layout layout;
        rv_editor_layout_preset_make(preset, panes, layout);
        RV_EDITOR_CHECK(!layout.nodes.empty());
        RV_EDITOR_CHECK(!panes.panes.empty());
    }
}
