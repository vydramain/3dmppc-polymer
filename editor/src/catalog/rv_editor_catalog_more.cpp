// The catalog's sections on focus and keys, scrolling and overflow, code text,
// the game frame, catalog cells and drop pockets, fonts, contrast and the
// thumbwheel: each drawn with the widget the editor uses, on fixed data.

#include "catalog/rv_editor_catalog.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "font/rv_editor_font.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_thumbwheel.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_catalog_more_data
{
    bool checks[3] = { false, true, false };
    int radio = 1;
    char pocket[64] = {};
    float wheel = 0.0f;
    char editable[32] = {};
    char locked[32] = {};
    bool samples_filled = false;
};

rv_editor_catalog_more_data rv_editor_catalog_more;

// Test frame dimensions in pixels
constexpr int game_frame_width_px = 320;
constexpr int game_frame_height_px = 240;
constexpr int game_frame_cell_size_px = 20;

// Test frame grid dimensions
constexpr int game_frame_grid_cols = game_frame_width_px / game_frame_cell_size_px;
constexpr int game_frame_grid_rows = game_frame_height_px / game_frame_cell_size_px;
constexpr int game_frame_center_x_px = game_frame_width_px / 2;
constexpr int game_frame_center_y_px = game_frame_height_px / 2;

// Test frame alpha channels
constexpr ImU32 game_frame_alpha_stale = 0x60000000u;
constexpr ImU32 game_frame_alpha_live = 0xff000000u;

// Test frame cell and border colors
constexpr ImU32 game_frame_cell_color_light = 0x00604030u;
constexpr ImU32 game_frame_cell_color_dark = 0x00306080u;
constexpr ImU32 game_frame_line_rgb = 0x00ffffffu;
constexpr int checker_tone_count = 2;

// Scroll region heights in frame heights
constexpr float scroll_height_frames = 4.0f;
constexpr float transport_strip_width_px = 220.0f;
constexpr float transport_bar_height_frames = 3.2f;

// Code display layout
constexpr float code_tile_height_lines = 7.5f;
constexpr float code_line_gap_px = 2.0f;
constexpr float code_lines_left_margin_px = 36.0f;
constexpr float code_line_number_top_offset_px = 4.0f;
constexpr float code_line_number_left_offset_px = 4.0f;
constexpr float code_selection_width_px = 80.0f;
constexpr float code_cursor_width_px = 2.0f;
constexpr float code_error_indicator_offset_px = 12.0f;
constexpr int code_sample_cursor_row = 3;
constexpr int code_sample_error_row = 5;
constexpr int overflow_sample_rows = 12;
constexpr int checkbox_snap_disabled_index = 2;
constexpr int broken_cell_index = 4;

// Game sample display heights in frame heights
constexpr float game_sample_base_height_frames = 7.0f;
constexpr float game_sample_fit_height_frames = 1.9f;
constexpr float game_sample_stale_height_frames = 1.4f;

// Asset cell display
constexpr float asset_cell_size_em = 6.0f;
constexpr float asset_cell_height_ratio = 0.7f;
constexpr float asset_icon_x_offset_ratio = 0.4f;
constexpr float asset_icon_y_offset_px = 4.0f;
constexpr float asset_name_x_offset_px = 2.0f;
constexpr float asset_name_y_offset_ratio = 0.45f;

// Text field and thumbwheel widths in ems
constexpr float text_field_sample_width_em = 9.0f;
constexpr float asset_pocket_width_em = 14.0f;
constexpr float thumbwheel_width_em = 14.0f;
constexpr float thumbwheel_speed = 0.4f;
constexpr float thumbwheel_angle_modulo_degrees = 360.0f;

// The test frame: 320x240 cells of 20 px in two tones, a white border and a centre cross.
void rv_editor_catalog_frame(ImDrawList *dl, ImVec2 p0, float scale, bool stale, const rv_editor_theme &t)
{
    const ImU32 alpha = stale ? game_frame_alpha_stale : game_frame_alpha_live;
    for (int y = 0; y < game_frame_grid_rows; ++y) {
        for (int x = 0; x < game_frame_grid_cols; ++x) {
            const bool light_cell = (x + y) % checker_tone_count == 0;
            const ImU32 c = (light_cell ? game_frame_cell_color_light : game_frame_cell_color_dark) | alpha;
            dl->AddRectFilled(ImVec2(p0.x + x * game_frame_cell_size_px * scale,
                                  p0.y + y * game_frame_cell_size_px * scale),
                ImVec2(p0.x + (x + 1) * game_frame_cell_size_px * scale,
                    p0.y + (y + 1) * game_frame_cell_size_px * scale),
                c);
        }
    }
    dl->AddRect(p0, ImVec2(p0.x + game_frame_width_px * scale, p0.y + game_frame_height_px * scale),
        game_frame_line_rgb | alpha);
    dl->AddLine(ImVec2(p0.x + game_frame_center_x_px * scale, p0.y),
        ImVec2(p0.x + game_frame_center_x_px * scale, p0.y + game_frame_height_px * scale),
        game_frame_line_rgb | alpha);
    dl->AddLine(ImVec2(p0.x, p0.y + game_frame_center_y_px * scale),
        ImVec2(p0.x + game_frame_width_px * scale, p0.y + game_frame_center_y_px * scale),
        game_frame_line_rgb | alpha);
    (void)t;
}

} // namespace

void rv_editor_catalog_keys(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    ImGui::TextWrapped("%s", rv_editor_text("catalog_more.keys_help"));
    const char *labels[] = { rv_editor_text("catalog_more.checkbox_mute"),
        rv_editor_text("catalog_more.checkbox_start_paused"),
        rv_editor_text("catalog_more.checkbox_snap_disabled") };
    for (int i = 0; i < static_cast<int>(std::size(labels)); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const char *reason = i == checkbox_snap_disabled_index ? rv_editor_text("catalog_more.disabled_shows_reason") : nullptr;
        rv_editor_checkbox(labels[i], &d.checks[i], theme,
            { rv_editor_look::live, reason });
    }
    const char *radios[] = { rv_editor_text("catalog_more.radio_select"),
        rv_editor_text("catalog_more.radio_move"), rv_editor_text("catalog_more.radio_rotate"),
        rv_editor_text("catalog_more.radio_scale") };
    for (int i = 0; i < static_cast<int>(std::size(radios)); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_radio(radios[i], d.radio == i, theme)) {
            d.radio = i;
        }
    }
}

void rv_editor_catalog_overflow(const rv_editor_theme &theme)
{
    // A scrolled region with the editor's own bars, both ways.
    rv_editor_scroll_begin("##cat_scroll",
        ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * scroll_height_frames), true,
        ImGuiChildFlags_Borders);
    for (int i = 1; i <= overflow_sample_rows; ++i) {
        const auto args = std::make_format_args(i, i, i, i, i);
        const auto text = rv_editor_text_format("catalog_more.overflow_long_line", args);
        ImGui::Text("%s", text.c_str());
    }
    rv_editor_scroll_end(theme);
    // The transport in a narrow strip: what does not fit goes behind More.
    ImGui::TextUnformatted(rv_editor_text("catalog_more.overflow_transport_label"));
    ImGui::BeginChild("##cat_narrow",
        ImVec2(transport_strip_width_px, ImGui::GetFrameHeight() * transport_bar_height_frames),
        ImGuiChildFlags_Borders);
    const rv_editor_transport_state state{ nullptr,
        rv_editor_text("catalog_more.transport_already_running"), nullptr,
        rv_editor_text("catalog_more.transport_pause_first"), nullptr, nullptr, false };
    rv_editor_transport_bar(state, theme);
    ImGui::EndChild();
}

void rv_editor_catalog_code(const rv_editor_theme &theme)
{
    // The code font and the colours the code tile's nvim draws with; the tile itself needs a running nvim.
    ImGui::TextDisabled("%s", rv_editor_text("catalog_more.code_tile_description"));
    const float w = ImGui::GetContentRegionAvail().x;
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 p0 = rv_editor_catalog_reserve(ImVec2(w, line * code_tile_height_lines));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + line * code_tile_height_lines), rv_editor_col(theme.code_base));
    rv_editor_font_code_push();
    const float cl = ImGui::GetTextLineHeight() + code_line_gap_px;
    struct piece
    {
        const char *text;
        uint32_t color;
    };
    const piece rows[][4] = {
        { { "local ", theme.code_magenta }, { "M", theme.code_text }, { " = {}", theme.code_text }, { "", 0 } },
        { { "function ", theme.code_magenta }, { "M.frame_update", theme.code_blue }, { "(dt)", theme.code_text },
            { "", 0 } },
        { { "  -- ", theme.code_subtext }, { "\xd0\xba\xd0\xb0\xd0\xb4\xd1\x80: Cyrillic in a comment", theme.code_subtext },
            { "", 0 }, { "", 0 } },
        { { "  state.x = state.x + ", theme.code_text }, { "1.5", theme.code_yellow }, { " * dt", theme.code_text },
            { "", 0 } },
        { { "#include ", theme.code_magenta }, { "\"pdk/rv_pdko.h\"", theme.code_green }, { "", 0 }, { "", 0 } },
        { { "constexpr int32_t ", theme.code_magenta }, { "DEPTH", theme.code_text },
            { " = 400; int broken =", theme.code_text }, { "", 0 } },
    };
    for (int r = 0; r < static_cast<int>(std::size(rows)); ++r) {
        float x = p0.x + code_lines_left_margin_px;
        const float y = p0.y + code_line_number_top_offset_px + r * cl;
        char number[8];
        std::snprintf(number, sizeof(number), "%2d", r + 1);
        dl->AddText(ImVec2(p0.x + code_line_number_left_offset_px, y), rv_editor_col(theme.code_subtext), number);
        if (r == code_sample_cursor_row) {
            // The selection and the cursor.
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + code_selection_width_px, y + cl),
                rv_editor_col(theme.code_surface));
        }
        for (const piece &p : rows[r]) {
            if (p.text[0] == '\0') {
                continue;
            }
            dl->AddText(ImVec2(x, y), rv_editor_col(p.color), p.text);
            x += ImGui::CalcTextSize(p.text).x;
        }
        if (r == code_sample_error_row) {
            // A build diagnostic under its place.
            dl->AddLine(ImVec2(p0.x + code_lines_left_margin_px, y + cl - 1.0f), ImVec2(x, y + cl - 1.0f),
                rv_editor_col(theme.code_red), 1.0f);
            dl->AddText(ImVec2(x + code_error_indicator_offset_px, y), rv_editor_col(theme.code_red),
                rv_editor_text("catalog_more.code_error_example"));
        }
        if (r == code_sample_cursor_row) {
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + code_cursor_width_px, y + cl),
                rv_editor_col(theme.code_text));
        }
    }
    rv_editor_font_code_pop();
}

void rv_editor_catalog_game(const rv_editor_theme &theme)
{
    // The Game tile's own placement on a 320x240 test frame, in two areas and two modes, and a stale frame.
    const float h = ImGui::GetFrameHeight() * game_sample_base_height_frames;
    const struct
    {
        const char *label;
        rv_editor_game_scale mode;
        float w;
        bool stale;
    } cases[] = { { "catalog_more.game_fit", rv_editor_game_scale::fit, h * game_sample_fit_height_frames,
                      false },
        { "catalog_more.game_integer", rv_editor_game_scale::integer, h * game_sample_fit_height_frames, false },
        { "catalog_more.game_fit_stale", rv_editor_game_scale::fit, h * game_sample_stale_height_frames, true } };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (const auto &c : cases) {
        ImGui::BeginGroup();
        const rv_editor_game_view view = rv_editor_game_place(game_frame_width_px, game_frame_height_px,
            c.w, h, c.mode);
        const char *label = rv_editor_text(c.label);
        const auto view_args = std::make_format_args(label, view.scale);
        const auto view_text = rv_editor_text_format(
            c.stale ? "catalog_more.game_view_stale" : "catalog_more.game_view", view_args);
        ImGui::Text("%s", view_text.c_str());
        const ImVec2 p0 = rv_editor_catalog_reserve(ImVec2(c.w, h));
        dl->AddRectFilled(p0, ImVec2(p0.x + c.w, p0.y + h), rv_editor_col(theme.code_base));
        rv_editor_catalog_frame(dl, ImVec2(p0.x + view.x, p0.y + view.y), view.scale, c.stale, theme);
        ImGui::EndGroup();
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

void rv_editor_catalog_cells(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    // Catalog cells of each kind, one whose picture failed, and a drop pocket to drop them on.
    const char *files[] = { rv_editor_text("catalog_more.asset_sprite_png"),
        rv_editor_text("catalog_more.asset_tone_pcm"), rv_editor_text("catalog_more.asset_main_lua"),
        rv_editor_text("catalog_more.asset_main_scene_toml"),
        rv_editor_text("catalog_more.asset_broken_png_text") };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float cell = ImGui::GetFontSize() * asset_cell_size_em;
    for (int i = 0; i < static_cast<int>(std::size(files)); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cell", ImVec2(cell, cell * asset_cell_height_ratio));
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("RV_ASSET", files[i], std::strlen(files[i]) + 1);
            ImGui::TextUnformatted(files[i]);
            ImGui::EndDragDropSource();
        }
        const char *code = "";
        uint32_t color = 0;
        rv_editor_file_chip(files[i], code, color);
        dl->AddText(ImVec2(p0.x + cell * asset_icon_x_offset_ratio, p0.y + asset_icon_y_offset_px),
            rv_editor_col(i == broken_cell_index ? theme.text_disabled : color), code);
        dl->AddText(ImVec2(p0.x + asset_name_x_offset_px, p0.y + cell * asset_name_y_offset_ratio),
            rv_editor_col(i == broken_cell_index ? theme.warning : theme.text), files[i]);
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * asset_pocket_width_em);
    rv_editor_text_field("##pocket", d.pocket, sizeof(d.pocket), theme);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("RV_ASSET")) {
            std::snprintf(d.pocket, sizeof(d.pocket), "%s", static_cast<const char *>(p->Data));
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(rv_editor_text("catalog_more.cells_drop_pocket"));
}

void rv_editor_catalog_type(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    // Fill the sample texts once; after that the fields keep what was typed.
    if (!d.samples_filled) {
        std::strncpy(d.editable, rv_editor_text("catalog_more.editable_sample"), sizeof(d.editable));
        std::strncpy(d.locked, rv_editor_text("catalog_more.readonly_sample"), sizeof(d.locked));
        d.samples_filled = true;
    }
    const float font_size = ImGui::GetFontSize();
    const auto font_args = std::make_format_args(font_size);
    const auto font_text = rv_editor_text_format("catalog_more.type_ui_font_description", font_args);
    ImGui::Text("%s", font_text.c_str());
    ImGui::TextUnformatted(rv_editor_text("catalog_more.type_interface_font"));
    rv_editor_font_code_push();
    ImGui::TextUnformatted(rv_editor_text("catalog_more.type_code_font"));
    rv_editor_font_code_pop();
    // Editable next to read-only: the two must not be mistaken.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * text_field_sample_width_em);
    rv_editor_text_field("##editable", d.editable, sizeof(d.editable), theme);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * text_field_sample_width_em);
    rv_editor_text_field("##locked", d.locked, sizeof(d.locked), theme, { {}, true });
    // The viewer's thumbwheel.
    bool reset = false;
    d.wheel += rv_editor_thumbwheel("##cat_wheel", rv_editor_text("catalog_more.type_thumbwheel_label"), false,
        ImGui::GetFontSize() * thumbwheel_width_em, theme, reset);
    if (reset) {
        d.wheel = 0.0f;
    }
    ImGui::SameLine();
    const float wheel_angle = std::fmod(d.wheel * thumbwheel_speed, thumbwheel_angle_modulo_degrees);
    const auto status_args = std::make_format_args(wheel_angle);
    const auto status_text = rv_editor_text_format("catalog_more.type_thumbwheel_status", status_args);
    ImGui::Text("%s", status_text.c_str());
}

} // namespace rv_editor
