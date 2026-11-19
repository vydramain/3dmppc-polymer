// The kinds whose pane body is built from the tiled UI templates.

#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

// The kinds built from the tiled UI templates (S13 on); each is listed as its pane is converted.
bool rv_editor_pane_converted(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::output:
    case rv_editor_pane_kind::runtime_log:
    case rv_editor_pane_kind::build_log:
    case rv_editor_pane_kind::terminal:
    case rv_editor_pane_kind::problems:
    case rv_editor_pane_kind::search:
    case rv_editor_pane_kind::code:
    case rv_editor_pane_kind::review_changes:
    case rv_editor_pane_kind::scene:
    case rv_editor_pane_kind::hierarchy:
    case rv_editor_pane_kind::inspector:
    case rv_editor_pane_kind::controls:
    case rv_editor_pane_kind::release_controls:
    case rv_editor_pane_kind::toolchest:
    case rv_editor_pane_kind::game:
    case rv_editor_pane_kind::export_:
    case rv_editor_pane_kind::candidate:
    case rv_editor_pane_kind::checks:
    case rv_editor_pane_kind::observe:
    case rv_editor_pane_kind::session:
    case rv_editor_pane_kind::build_result:
    case rv_editor_pane_kind::empty:
    case rv_editor_pane_kind::project:
    case rv_editor_pane_kind::files:
    case rv_editor_pane_kind::assets:
    case rv_editor_pane_kind::run_config:
    case rv_editor_pane_kind::findings:
    case rv_editor_pane_kind::test_case:
    case rv_editor_pane_kind::new_project:
    case rv_editor_pane_kind::open_project:
    case rv_editor_pane_kind::settings:
    case rv_editor_pane_kind::help:
    case rv_editor_pane_kind::manual:
        return true;
    default:
        return false;
    }
}

} // namespace rv_editor
