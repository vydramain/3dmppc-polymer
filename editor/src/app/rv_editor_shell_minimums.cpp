// Minimum tile size per pane kind, so a default layout's controls stay visible
// at the smallest supported window (1200x800, Steam Deck).

#include "app/rv_editor_shell.hpp"

#include <algorithm>

#include "imgui.h"

namespace rv_editor
{

namespace
{

// The log panes' one control row, down to Source + Level + Find (at its own
// minimum) + More, once Follow, Wrap, Copy, Export and Clear View have all
// dropped behind More (rv_editor_output_controls); a few lines under it.
// Kinds not listed have no need of their own: the tree's generic per-tile
// minimum (rv_editor_workspace_draw) already covers them.
rv_editor_size rv_editor_shell_pane_minimum(rv_editor_pane_kind kind)
{
    const float fs = ImGui::GetFontSize();
    const float fh = ImGui::GetFrameHeight();
    switch (kind) {
        case rv_editor_pane_kind::output:
        case rv_editor_pane_kind::runtime_log:
        case rv_editor_pane_kind::build_log: return { static_cast<int32_t>(fs * 30.0f), static_cast<int32_t>(fh * 6.0f) };
        default: return { 0, 0 };
    }
}

} // namespace

void rv_editor_shell_set_minimum(rv_editor_shell &shell, rv_editor_pane_id pane, rv_editor_pane_kind kind,
    rv_editor_size game_need)
{
    rv_editor_size need = rv_editor_shell_pane_minimum(kind);
    if (kind == rv_editor_pane_kind::game && game_need.w > 0) {
        need = { std::max(need.w, game_need.w), std::max(need.h, game_need.h) };
    }
    // A strip of controls is as tall as its rows, whatever its split's ratio says.
    const auto strip = shell.strips.find(pane);
    if (strip != shell.strips.end()) {
        need = { std::max(need.w, strip->second.w), std::max(need.h, strip->second.h) };
    }
    if (need.w > 0 || need.h > 0) {
        shell.ws.minimums[pane] = need;
    }
}

} // namespace rv_editor
