// Empty pane: displays selectable pane kinds to initialize the pane.

#include "panes/rv_editor_panes.hpp"

#include "app/rv_editor_shell.hpp"
#include "imgui.h"
#include "layout/rv_editor_tile.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

void rv_editor_pane_empty(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    ImGui::TextWrapped("%s", rv_editor_text("pane_empty.choose_pane_kind"));
    ImGui::Spacing();

    // Display all kinds except empty in a grid of buttons.
    bool first_button = true;
    for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
        const auto kind = static_cast<rv_editor_pane_kind>(k);
        if (kind == rv_editor_pane_kind::empty) {
            continue;
        }
        const char *label = rv_editor_pane_title(kind);
        if (!first_button) {
            rv_editor_flow(rv_editor_button_width(label));
        }
        first_button = false;
        if (rv_editor_button(label, theme)) {
            shell.ws.pending.what = rv_editor_tile_action::op::set_kind;
            shell.ws.pending.pane = pane;
            shell.ws.pending.kind = kind;
        }
    }
}

} // namespace rv_editor
