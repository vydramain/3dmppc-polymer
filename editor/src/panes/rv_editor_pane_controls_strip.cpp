// Runtime Controls' strip measures: its least width and its height at a width.

#include <algorithm>

#include "imgui.h"
#include "panes/rv_editor_pane_controls.hpp"
#include "pdklib/rv_progress/rv_progress.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

namespace rv_editor
{

namespace
{

// The width the longest status needs to wrap into its two lines: every session state with a six digit number, then each
// message's brief or a build's number and a 16 character step.
float rv_editor_status_min_width()
{
    rv_editor_controls_message_input hung, gone, unanswered, detail, restart, build_restart;
    hung.force_stop = hung.hung = true;
    gone.force_stop = true;
    unanswered.uncertain = true;
    detail.detail = "x";
    detail.outcome = rv_editor_controls_outcome::reload_refused;
    restart.offer_build_restart = restart.disc_cannot_reload = true;
    build_restart.offer_build_restart = true;
    float width = 0.0f;
    for (const auto &message : { hung, gone, unanswered, detail, restart, build_restart, rv_editor_controls_message_input{} }) {
        for (int state = 0; state <= static_cast<int>(rv_editor_run_state::refused); ++state) {
            rv_editor_controls_status_input in;
            in.run_state = static_cast<rv_editor_run_state>(state);
            in.session_number = 999999;
            in.build_number = 999999;
            in.build_step = "[v][>][ ] xxxxxxxxxxxxxxxx";
            in.message = message;
            width = std::max(width, rv_editor_strip_status_width_for(rv_editor_controls_status(in)));
        }
    }
    return width;
}

// The UI font is current while measuring: main sets the minimums outside a frame, where ImGui has no current font.
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

} // namespace

float rv_editor_transport_column_min_width()
{
    const ui_font_scope font;
    return rv_editor_controls_action_slot_width(rv_editor_theme_olive) + ImGui::GetStyle().ItemSpacing.x +
        rv_editor_button_width(rv_editor_text("panes.force_stop_button"));
}

float rv_editor_playback_build_status_min_width()
{
    constexpr uint32_t number = 999999;
    const std::string target = rv_editor_text_format("panes.status_target_candidate", std::make_format_args(number));
    const std::string build = target + " step 99/99: " + std::string(rv_pdklib::rv_progress_step_name_max, 'W');
    float width = 0.0f;
    for (int i = 0; i <= static_cast<int>(rv_editor_run_state::refused); ++i) {
        const char *state = rv_editor_run_state_name(static_cast<rv_editor_run_state>(i));
        const std::string session = rv_editor_text_format("panes.status_session", std::make_format_args(state, number));
        const std::string playback = rv_editor_text_format("panes.status_join", std::make_format_args(session, target));
        const std::string status = rv_editor_text_format("panes.status_join", std::make_format_args(playback, build));
        width = std::max(width, rv_editor_strip_status_width_for(status));
    }
    return width + 1.0f; // the last glyph must lie inside the child's clip rectangle
}

float rv_editor_transport_status_min_width()
{
    const ui_font_scope font;
    // Asked every frame; the answer changes only with the font or its size.
    static const ImFont *cached_font = nullptr;
    static float cached_size = 0.0f;
    static float cached = 0.0f;
    const ImFont *ui = ImGui::GetIO().Fonts->Fonts[0];
    if (ui != cached_font || ui->LegacySize != cached_size) {
        cached = std::max(rv_editor_status_min_width(), rv_editor_playback_build_status_min_width());
        cached_font = ui;
        cached_size = ui->LegacySize;
    }
    return cached;
}

float rv_editor_transport_min_width()
{
    const ui_font_scope font;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x;
    const float buttons = 6.0f * ImGui::GetFrameHeight() + 5.0f * gap;
    // The window's left padding and the shelf's, the buttons, then the action slot; the slot reaches the tile's edge at this
    // width, so no right padding is counted; so does the status under them, which wraps its longest text into two lines.
    const float row = style.WindowPadding.x + style.ItemInnerSpacing.x + buttons + gap + rv_editor_transport_column_min_width();
    return std::max(row, rv_editor_transport_status_min_width() + style.WindowPadding.x + style.ItemInnerSpacing.x);
}

float rv_editor_transport_height(float width)
{
    const ui_font_scope font;
    const ImGuiStyle &style = ImGui::GetStyle();
    // The window's padding, the shelf's padding on both sides (the style's inner spacing is the theme's pad), the row of
    // tiles (the slot is no taller), the status's two lines.
    return style.WindowPadding.y + 2.0f * style.ItemInnerSpacing.y +
        rv_editor_transport_side(width, rv_editor_transport_column_min_width()) + style.ItemSpacing.y +
        rv_editor_strip_status_height();
}

} // namespace rv_editor
