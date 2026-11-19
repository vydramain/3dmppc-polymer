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

// Minimum width in font sizes of a strip of controls.
constexpr int strip_min_width_em = 20;
// Minimum width in font sizes and height in frame heights of the Widget Catalog.
constexpr float catalog_min_width_em = 20.0f;
constexpr float catalog_min_height_frames = 8.0f;
// The Code tile's row keeps room for the tabs' three buttons and one tab, in frame heights.
constexpr float code_row_frames = 4.0f;

// A shelf of `rows` height as the tile lays it out: its padding on both sides and the spacing the
// tile's content ends with. The theme's pad is half the window padding the style gets from it.
float shelf_height(float rows)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    return style.WindowPadding.y + rows + style.ItemSpacing.y;
}

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
    const float gap = style.ItemSpacing.y;
    const float large = rv_editor_tile_large_side(font, fs);
    const int32_t strip_w = static_cast<int32_t>(fs * strip_min_width_em);
    switch (kind) {
    // A strip is as tall as its tallest state at its minimum width, not as what it drew this frame. Controls: the
    // transport's own measures.
    case rv_editor_pane_kind::controls: {
        const float width = rv_editor_transport_min_width();
        return { static_cast<int32_t>(std::ceil(width)), static_cast<int32_t>(std::ceil(rv_editor_transport_height(width))) };
    }
    case rv_editor_pane_kind::release_controls:
        return { strip_w, static_cast<int32_t>(std::ceil(shelf_height(large))) };
    // The scene tools' two rows, or the wrapped line of no scene when it is taller (the debug mark row is
    // a frame and a text line, less than the scene tools).
    case rv_editor_pane_kind::toolchest: {
        const float wrap = strip_w - 2.0f * style.WindowPadding.x - style.WindowPadding.y;
        const float no_scene = font->CalcTextSizeA(fs, FLT_MAX, wrap, rv_editor_text("pane_toolchest.no_scene")).y;
        return { strip_w, static_cast<int32_t>(std::ceil(shelf_height(std::max(2.0f * fh + gap, no_scene)))) };
    }
    // The mode row's shelf, the well's padding around the picture and the status row over it.
    case rv_editor_pane_kind::game:
        return { static_cast<int32_t>(std::ceil(rv_editor_game_picture_min_width() + 2.0f * style.WindowPadding.x)),
            static_cast<int32_t>(std::ceil(shelf_height(fh) + style.WindowPadding.y + (fs + gap))) };
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
