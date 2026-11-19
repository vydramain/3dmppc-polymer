// Minimum tile size per pane kind, so a default layout's controls stay visible
// at the smallest supported window (1280x720 at UI scale 1).

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "panes/rv_editor_game_status.hpp"
#include "panes/rv_editor_pane_controls.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_mode.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// Minimum width in font sizes and height in frame heights of the Widget Catalog.
constexpr float catalog_min_width_em = 20.0f;
constexpr float catalog_min_height_frames = 8.0f;
// The Code tile's row keeps room for the tabs' three buttons and one tab, in frame heights.
constexpr float code_row_frames = 4.0f;

// Pushes the UI font for a measure taken before the first frame or outside one.
struct ui_font_scope {
    ui_font_scope()
    {
        ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
        ImGui::PushFont(font, font->LegacySize);
    }
    ~ui_font_scope()
    {
        ImGui::PopFont();
    }
    ui_font_scope(const ui_font_scope &) = delete;
    ui_font_scope &operator=(const ui_font_scope &) = delete;
};

// The biggest ask card of the Code tile and of Review Changes (Unsaved changes, Save As, the swap file ask, a start that
// failed): every one is as high as the card's minimum, and as wide as its buttons.
ImVec2 ask_card_size()
{
    const rv_editor_theme theme{};
    ImVec2 card(0.0f, 0.0f);
    const std::vector<std::vector<std::string>> rows = {
        { rv_editor_text("shell_save.save_button"),
            rv_editor_text("shell_save.discard_button"),
            rv_editor_text("shell_save.keep_open_button") },
        { rv_editor_text("shell_save.save_button"), rv_editor_text("shell_save.cancel_button") },
        { rv_editor_text("pane_code.swap_recover_button"), rv_editor_text("pane_code.swap_discard_button") },
        { rv_editor_text("pane_code.start_nvim_again") },
    };
    for (const std::vector<std::string> &buttons : rows) {
        const ImVec2 one = rv_editor_mode_card_min_size(buttons, theme);
        card = ImVec2(std::max(card.x, one.x), std::max(card.y, one.y));
    }
    return card;
}

// The Code tile: the row of its files, then the widest card under it. The row keeps the Vim switch and room for the
// tabs' arrows and one tab.
rv_editor_size code_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 card = ask_card_size();
    const float row = rv_editor_button_width(rv_editor_text("pane_code.vim_toggle")) + style.ItemSpacing.x +
        code_row_frames * ImGui::GetFrameHeight();
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + std::max(card.x, rv_editor_tiled_shelf_width(row)))),
        static_cast<int32_t>(std::ceil(style.WindowPadding.y + rv_editor_tiled_shelf_block() + card.y)) };
}

// Review Changes: its shelf, message box and content area, and a Save As card instead of them.
rv_editor_size review_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 card = ask_card_size();
    const rv_editor_size min =
        rv_editor_tiled_minimum(rv_editor_review_content_width(), rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block());
    return { std::max(min.w, static_cast<int32_t>(std::ceil(style.WindowPadding.x + card.x))),
        std::max(min.h, static_cast<int32_t>(std::ceil(2.0f * style.WindowPadding.y + card.y))) };
}

// Kinds not listed have no need of their own: the tree's generic per-tile
// minimum (rv_editor_workspace_draw) already covers them.
rv_editor_size rv_editor_shell_pane_minimum(rv_editor_pane_kind kind)
{
    // Called before the first frame, with no current font: measure with the UI font explicitly.
    ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
    const float fs = font->LegacySize;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float fh = fs + 2.0f * style.FramePadding.y;
    switch (kind) {
    // A strip is as tall as its tallest state at its minimum width, not as what it drew this frame. Controls: the
    // transport's own measures.
    case rv_editor_pane_kind::controls: {
        const float width = rv_editor_transport_min_width();
        return { static_cast<int32_t>(std::ceil(width)), static_cast<int32_t>(std::ceil(rv_editor_transport_height(width))) };
    }
    case rv_editor_pane_kind::release_controls: {
        const ui_font_scope scope;
        return rv_editor_release_controls_minimum();
    }
    case rv_editor_pane_kind::toolchest: {
        const ui_font_scope scope;
        return rv_editor_toolchest_minimum();
    }
    case rv_editor_pane_kind::game: {
        const ui_font_scope scope;
        return rv_editor_game_minimum();
    }
    // The catalog scrolls as a whole, so any size shows it whole: room for the section tiles' headings and a few rows.
    case rv_editor_pane_kind::catalog:
        return { static_cast<int32_t>(fs * catalog_min_width_em), static_cast<int32_t>(fh * catalog_min_height_frames) };
    // The tiled panes: a function of their own each, the sum of what they draw in their widest state.
    case rv_editor_pane_kind::output:
    case rv_editor_pane_kind::runtime_log:
    case rv_editor_pane_kind::build_log: {
        const ui_font_scope scope;
        return rv_editor_output_minimum();
    }
    case rv_editor_pane_kind::terminal: {
        const ui_font_scope scope;
        return rv_editor_terminal_minimum();
    }
    case rv_editor_pane_kind::problems: {
        const ui_font_scope scope;
        return rv_editor_problems_minimum();
    }
    case rv_editor_pane_kind::search: {
        const ui_font_scope scope;
        return rv_editor_search_minimum();
    }
    case rv_editor_pane_kind::code: {
        const ui_font_scope scope;
        return code_minimum();
    }
    case rv_editor_pane_kind::review_changes: {
        const ui_font_scope scope;
        return review_minimum();
    }
    case rv_editor_pane_kind::scene: {
        const ui_font_scope scope;
        return rv_editor_scene_minimum();
    }
    case rv_editor_pane_kind::hierarchy: {
        const ui_font_scope scope;
        return rv_editor_hierarchy_minimum();
    }
    case rv_editor_pane_kind::inspector: {
        const ui_font_scope scope;
        return rv_editor_inspector_minimum();
    }
    case rv_editor_pane_kind::export_: {
        const ui_font_scope scope;
        return rv_editor_export_minimum();
    }
    case rv_editor_pane_kind::candidate: {
        const ui_font_scope scope;
        return rv_editor_candidate_minimum();
    }
    case rv_editor_pane_kind::checks: {
        const ui_font_scope scope;
        return rv_editor_checks_minimum();
    }
    case rv_editor_pane_kind::observe: {
        const ui_font_scope scope;
        return rv_editor_observe_minimum();
    }
    case rv_editor_pane_kind::session: {
        const ui_font_scope scope;
        return rv_editor_session_minimum();
    }
    case rv_editor_pane_kind::build_result: {
        const ui_font_scope scope;
        return rv_editor_build_result_minimum();
    }
    case rv_editor_pane_kind::empty: {
        const ui_font_scope scope;
        return rv_editor_empty_minimum();
    }
    case rv_editor_pane_kind::project: {
        const ui_font_scope scope;
        return rv_editor_project_minimum();
    }
    case rv_editor_pane_kind::files: {
        const ui_font_scope scope;
        return rv_editor_files_minimum();
    }
    case rv_editor_pane_kind::assets: {
        const ui_font_scope scope;
        return rv_editor_assets_minimum();
    }
    case rv_editor_pane_kind::run_config: {
        const ui_font_scope scope;
        return rv_editor_run_config_minimum();
    }
    case rv_editor_pane_kind::findings: {
        const ui_font_scope scope;
        return rv_editor_findings_minimum();
    }
    case rv_editor_pane_kind::test_case: {
        const ui_font_scope scope;
        return rv_editor_test_case_minimum();
    }
    default:
        return { 0, 0 };
    }
}

} // namespace

void rv_editor_shell_set_minimum(rv_editor_shell &shell,
    rv_editor_pane_id pane,
    rv_editor_pane_kind kind,
    rv_editor_size /*game_need*/)
{
    const rv_editor_size need = rv_editor_shell_pane_minimum(kind);
    if (need.w > 0 || need.h > 0) {
        shell.ws.minimums[pane] = need;
    }
}

} // namespace rv_editor
