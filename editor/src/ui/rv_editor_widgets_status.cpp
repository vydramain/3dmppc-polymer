// Status indicators, log rows, the transport bar and dialogs.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// Style colours rv_editor_log_begin pushes (child background, text).
constexpr int log_style_pushed_count = 2;
// Gap between status fields in scaled pixels.
constexpr int status_bar_gap_px = 2;
// Theme pads added to each status field's text width.
constexpr float status_bar_field_pads = 4.0f;
// Theme pads from a field's left edge to its text.
constexpr float status_bar_text_inset_pads = 2.0f;
// Divisor for centering by halving.
constexpr float half_divisor = 2.0f;
// The window width, before scale, from which the transport buttons are large and carry labels.
constexpr float transport_labels_from_px = 640.0f;

// The scale is the style's inner spacing over the theme's unscaled pad, as the style is built from the theme: the
// width functions have no theme.
bool transport_is_large(float width)
{
    const float scale = ImGui::GetStyle().ItemInnerSpacing.x / static_cast<float>(rv_editor_theme_olive.pad_px);
    return width >= transport_labels_from_px * scale;
}

// Every state has a symbol as well as a colour, so none is told by colour alone.
struct rv_editor_mark {
    const char *symbol;
    uint32_t color;
};

rv_editor_mark rv_editor_status_mark(rv_editor_status_kind kind, const rv_editor_theme &t)
{
    switch (kind) {
    case rv_editor_status_kind::idle:
        return { rv_editor_text("widgets_status.status_idle"), t.text_disabled };
    case rv_editor_status_kind::busy:
        return { rv_editor_text("widgets_status.status_busy"), t.selection };
    case rv_editor_status_kind::ok:
        return { rv_editor_text("widgets_status.status_ok"), t.ok };
    case rv_editor_status_kind::active:
        return { rv_editor_text("widgets_status.status_active"), t.ok };
    case rv_editor_status_kind::warning:
        return { rv_editor_text("widgets_status.status_warning"), t.warning };
    case rv_editor_status_kind::error:
        return { rv_editor_text("widgets_status.status_error"), t.error };
    }
    return { rv_editor_text("widgets_status.status_unknown"), t.text };
}

rv_editor_mark rv_editor_severity_mark(rv_editor_severity severity, const rv_editor_theme &t)
{
    switch (severity) {
    case rv_editor_severity::info:
        return { rv_editor_text("widgets_status.severity_info"), t.code_blue };
    case rv_editor_severity::warning:
        return { rv_editor_text("widgets_status.severity_warning"), t.code_yellow };
    case rv_editor_severity::error:
        return { rv_editor_text("widgets_status.severity_error"), t.code_red };
    }
    return { rv_editor_text("widgets_status.severity_unknown"), t.code_text };
}

} // namespace

void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme, bool frame_aligned)
{
    // Passive text, not a button face: a bracketed mark in the state's colour,
    // on a flat backing sized to the mark so it reads on any row background.
    const rv_editor_mark mark = rv_editor_status_mark(kind, theme);
    if (frame_aligned) {
        ImGui::AlignTextToFramePadding();
    }
    // AlignTextToFramePadding offsets the glyph baseline by FramePadding.y from
    // the cursor; match that so the backing lands exactly under the mark.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 mark_min(pos.x, pos.y + (frame_aligned ? ImGui::GetStyle().FramePadding.y : 0.0f));
    const ImVec2 size = ImGui::CalcTextSize(mark.symbol);
    ImGui::GetWindowDrawList()->AddRectFilled(mark_min,
        ImVec2(mark_min.x + size.x, mark_min.y + size.y),
        rv_editor_col(theme.dark));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(mark.color));
    ImGui::TextUnformatted(mark.symbol);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (frame_aligned) {
        ImGui::AlignTextToFramePadding();
    }
    ImGui::TextUnformatted(label); // one line, whole: a long status scrolls its content area
}

float rv_editor_status_width(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme)
{
    const rv_editor_mark mark = rv_editor_status_mark(kind, theme);
    return ImGui::CalcTextSize(mark.symbol).x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(label).x;
}

bool rv_editor_log_begin(const char *id, ImVec2 size, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_text));
    return rv_editor_scroll_begin(id, size, true);
}

void rv_editor_log_end(const rv_editor_theme &theme)
{
    rv_editor_scroll_end(theme);
    ImGui::PopStyleColor(log_style_pushed_count);
}

void rv_editor_log_row(const char *time,
    const char *source,
    rv_editor_severity severity,
    const char *text,
    const rv_editor_theme &theme)
{
    const rv_editor_mark mark = rv_editor_severity_mark(severity, theme);
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext));
    ImGui::TextUnformatted(time);
    ImGui::SameLine();
    ImGui::Text("[%s]", source);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(mark.color));
    ImGui::TextUnformatted(mark.symbol);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(text);
}

rv_editor_transport_actions
rv_editor_transport_bar(const rv_editor_transport_state &state, const rv_editor_theme &theme, float reserve)
{
    rv_editor_transport_actions out = {};
    // Step and Stop differ in code and colour, not in the label alone; the
    // square's label is short, its tooltip says the whole name.
    struct rv_editor_transport_button {
        rv_editor_tile tile;
        const char *disabled;
        bool *clicked;
    };
    // Reload's slot: Build and Restart when Reload cannot apply the pending change.
    rv_editor_transport_button reload_slot = {
        { rv_editor_text("widgets_status.button_reload"),
            rv_editor_glyph::reload,
            rv_editor_mocha_teal,
            state.reload_name != nullptr ? state.reload_name : rv_editor_text("widgets_status.tooltip_reload"),
            rv_editor_text("widgets_status.shortcut_reload") },
        state.reload,
        &out.reload
    };
    if (state.build_restart) {
        reload_slot = { { rv_editor_text("widgets_status.button_restart"),
                            rv_editor_glyph::restart,
                            rv_editor_mocha_red,
                            rv_editor_text("widgets_status.tooltip_restart") },
            state.build_restart_disabled,
            &out.build_restart };
    }
    const char *run_resume_label = rv_editor_text(state.resume ? "widgets_status.button_resume" : "widgets_status.button_run");
    const rv_editor_transport_button buttons[] = {
        { { rv_editor_text("widgets_status.button_build"),
              rv_editor_glyph::build,
              rv_editor_mocha_peach,
              rv_editor_text("widgets_status.button_build"),
              rv_editor_text("widgets_status.shortcut_build") },
            state.build,
            &out.build },
        { { run_resume_label,
              rv_editor_glyph::run,
              theme.code_green,
              run_resume_label,
              rv_editor_text("widgets_status.shortcut_run") },
            state.run,
            &out.run },
        { { rv_editor_text("widgets_status.button_pause"),
              rv_editor_glyph::pause,
              theme.code_yellow,
              rv_editor_text("widgets_status.button_pause"),
              rv_editor_text("widgets_status.shortcut_pause") },
            state.pause,
            &out.pause },
        { { rv_editor_text("widgets_status.button_step"),
              rv_editor_glyph::step_frame,
              theme.code_blue,
              rv_editor_text("widgets_status.button_step_frame"),
              rv_editor_text("widgets_status.shortcut_step") },
            state.step,
            &out.step },
        { { rv_editor_text("widgets_status.button_stop"),
              rv_editor_glyph::stop,
              theme.code_red,
              rv_editor_text("widgets_status.button_stop"),
              rv_editor_text("widgets_status.shortcut_stop") },
            state.stop,
            &out.stop },
        reload_slot,
    };
    // One row, never wrapped: the size follows the window width alone, so no state moves a button.
    const rv_editor_tile_size size =
        transport_is_large(ImGui::GetWindowWidth()) ? rv_editor_tile_size::large : rv_editor_tile_size::small;
    const float side = rv_editor_transport_side(ImGui::GetWindowWidth(), reserve);
    for (size_t i = 0; i < std::size(buttons); ++i) {
        auto tile = buttons[i].tile;
        tile.side = side;
        const auto &b = buttons[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        *b.clicked = rv_editor_tile_button(tile, size, theme, { rv_editor_look::live, b.disabled });
    }
    return out;
}

bool rv_editor_transport_is_large(float width)
{
    return transport_is_large(width);
}

float rv_editor_transport_side(float width, float reserve, int count)
{
    const float frame = ImGui::GetFrameHeight();
    if (!transport_is_large(width)) {
        return frame;
    }
    const ImGuiStyle &style = ImGui::GetStyle();
    const float natural = rv_editor_tile_large_side(ImGui::GetIO().Fonts->Fonts[0], ImGui::GetIO().Fonts->Fonts[0]->LegacySize);
    // The window's and the shelf's padding on both sides, and the gaps between the squares and the column.
    const float squares = static_cast<float>(count);
    const float room =
        width - 2.0f * (style.WindowPadding.x + style.ItemInnerSpacing.x) - squares * style.ItemSpacing.x - reserve;
    return std::max(frame, std::min(natural, std::floor(room / squares)));
}

void rv_editor_message_line(const char *id,
    const char *text,
    int lines,
    float width,
    const rv_editor_theme &theme,
    const char *tip,
    rv_editor_label_kind kind)
{
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float room = static_cast<float>(std::max(lines, 1)) * ImGui::GetTextLineHeight();
    const std::string whole = text != nullptr ? text : "";
    std::string shown = whole;
    // Longest prefix, cut at a character boundary, that wraps into `lines` lines with the dots; a binary search.
    if (ImGui::CalcTextSize(whole.c_str(), nullptr, false, w).y > room) {
        const auto fits = [&](size_t n) {
            return ImGui::CalcTextSize((whole.substr(0, n) + "...").c_str(), nullptr, false, w).y <= room;
        };
        const auto boundary = [&](size_t n) {
            while (n > 0 && (static_cast<unsigned char>(whole[n]) & 0xC0) == 0x80) {
                --n;
            }
            return n;
        };
        size_t lo = 0;
        size_t hi = whole.size();
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            if (fits(boundary(mid))) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        shown = whole.substr(0, boundary(lo)) + "...";
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(w, room), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!shown.empty()) {
        rv_editor_label(shown.c_str(), kind, theme);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    const std::string full = tip != nullptr && *tip != '\0' ? tip : whole;
    if (!whole.empty() && (shown != whole || full != whole) && ImGui::IsItemHovered()) {
        rv_editor_tooltip(full.c_str());
    }
}

namespace
{

// A segment's own width: its widest text and the paddings, in whole pixels.
float status_segment_base(const char *widest, const rv_editor_theme &theme)
{
    // The UI font explicitly: the width is also asked between frames, with no current font.
    ImFont *const font = ImGui::GetIO().Fonts->Fonts[0];
    const float text = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, widest).x;
    return std::ceil(text + static_cast<float>(theme.pad_px * theme.scale) * status_bar_field_pads);
}

} // namespace

float rv_editor_status_bar_min_width(const char *const widest[], int count, const rv_editor_theme &theme)
{
    float sum = 2.0f * ImGui::GetStyle().WindowPadding.x +
        (count > 1 ? static_cast<float>(count - 1) * static_cast<float>(status_bar_gap_px * theme.scale) : 0.0f);
    for (int i = 0; i < count; ++i) {
        sum += status_segment_base(widest[i], theme);
    }
    return sum;
}

int rv_editor_status_bar(const char *const fields[],
    const char *const widest[],
    int count,
    const rv_editor_theme &theme,
    rv_editor_status_probe *probe)
{
    if (count <= 0) {
        return -1;
    }
    const float h = ImGui::GetFrameHeight();
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float gap = static_cast<float>(status_bar_gap_px * theme.scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    // Each segment is its widest text plus the same share of the free width, so no text changes an edge.
    const float free_width =
        width - (rv_editor_status_bar_min_width(widest, count, theme) - 2.0f * ImGui::GetStyle().WindowPadding.x);
    const float share = std::max(0.0f, std::floor(free_width / static_cast<float>(count)));
    float width_cap = FLT_MAX;
    if (free_width < 0.0f) {
        // Fit the short fields first, then divide the remaining space evenly among the wider ones.
        std::vector<float> base_widths;
        for (int i = 0; i < count; ++i) {
            base_widths.push_back(status_segment_base(widest[i], theme));
        }
        std::sort(base_widths.begin(), base_widths.end());
        float remaining = width - gap * static_cast<float>(count - 1);
        for (int i = 0; i < count; ++i) {
            width_cap = std::floor(remaining / static_cast<float>(count - i));
            if (base_widths[i] >= width_cap) {
                break;
            }
            remaining -= base_widths[i];
        }
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    int clicked = -1;
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const float w = std::max(1.0f, std::min(status_segment_base(widest[i], theme), width_cap) + share);
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + h);
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton((std::string(fields[i]) + "##seg" + std::to_string(i)).c_str(), ImVec2(w, h))) {
            clicked = i;
        }
        rv_editor_draw_panel(dl, min, max, theme, theme.window, rv_editor_bevel::sunken);
        const ImVec2 text = ImGui::CalcTextSize(fields[i]);
        const float text_x = std::floor(min.x + pad * status_bar_text_inset_pads);
        const float text_y = std::floor((min.y + max.y - text.y) / half_divisor);
        const float text_room = max.x - text_x - pad * status_bar_text_inset_pads;
        const std::string shown = rv_editor_ellipsize(fields[i], text_room);
        const bool shortened = shown != fields[i];
        dl->AddText(ImVec2(text_x, text_y), rv_editor_col(theme.text), shown.c_str());
        if (shortened && ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width / half_divisor);
            ImGui::TextUnformatted(fields[i]);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (probe != nullptr) {
            probe->segments.push_back({ min.x, max.x, shown, shortened ? fields[i] : "" });
        }
        x = max.x + gap;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, h));
    return clicked;
}

void rv_editor_menu_style_push()
{
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_Header));
}

void rv_editor_menu_style_pop()
{
    ImGui::PopStyleColor();
}

} // namespace rv_editor
