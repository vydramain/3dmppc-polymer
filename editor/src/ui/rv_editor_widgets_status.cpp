// Status indicators, log rows, the transport bar and dialogs.

#include <algorithm>
#include <cmath>

#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"

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

void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme)
{
    // Passive text, not a button face: a bracketed mark in the state's colour,
    // on a flat backing sized to the mark so it reads on any row background.
    const rv_editor_mark mark = rv_editor_status_mark(kind, theme);
    ImGui::AlignTextToFramePadding();
    // AlignTextToFramePadding offsets the glyph baseline by FramePadding.y from
    // the cursor; match that so the backing lands exactly under the mark.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 mark_min(pos.x, pos.y + ImGui::GetStyle().FramePadding.y);
    const ImVec2 size = ImGui::CalcTextSize(mark.symbol);
    ImGui::GetWindowDrawList()->AddRectFilled(mark_min,
        ImVec2(mark_min.x + size.x, mark_min.y + size.y),
        rv_editor_col(theme.dark));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(mark.color));
    ImGui::TextUnformatted(mark.symbol);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
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
    // square's label is short, its tooltip and the More menu say the whole name.
    struct rv_editor_transport_button {
        const char *label;
        const char *code;
        uint32_t color;
        const char *name;
        const char *shortcut;
        const char *disabled;
        bool *clicked;
    };
    // Reload's slot: Build and Restart when Reload cannot apply the pending change.
    rv_editor_transport_button reload_slot = { rv_editor_text("widgets_status.button_reload"),
        rv_editor_glyph::reload,
        rv_editor_mocha_teal,
        state.reload_name != nullptr ? state.reload_name : rv_editor_text("widgets_status.tooltip_reload"),
        rv_editor_text("widgets_status.shortcut_reload"),
        state.reload,
        &out.reload };
    if (state.build_restart) {
        reload_slot = { rv_editor_text("widgets_status.button_restart"),
            rv_editor_glyph::restart,
            rv_editor_mocha_red,
            rv_editor_text("widgets_status.tooltip_restart"),
            nullptr,
            state.build_restart_disabled,
            &out.build_restart };
    }
    const char *run_resume_label = rv_editor_text(state.resume ? "widgets_status.button_resume" : "widgets_status.button_run");
    const rv_editor_transport_button buttons[] = {
        { rv_editor_text("widgets_status.button_build"),
            rv_editor_glyph::build,
            rv_editor_mocha_peach,
            rv_editor_text("widgets_status.button_build"),
            rv_editor_text("widgets_status.shortcut_build"),
            state.build,
            &out.build },
        { run_resume_label,
            rv_editor_glyph::run,
            theme.code_green,
            run_resume_label,
            rv_editor_text("widgets_status.shortcut_run"),
            state.run,
            &out.run },
        { rv_editor_text("widgets_status.button_pause"),
            rv_editor_glyph::pause,
            theme.code_yellow,
            rv_editor_text("widgets_status.button_pause"),
            rv_editor_text("widgets_status.shortcut_pause"),
            state.pause,
            &out.pause },
        { rv_editor_text("widgets_status.button_step"),
            rv_editor_glyph::step_frame,
            theme.code_blue,
            rv_editor_text("widgets_status.button_step_frame"),
            rv_editor_text("widgets_status.shortcut_step"),
            state.step,
            &out.step },
        { rv_editor_text("widgets_status.button_stop"),
            rv_editor_glyph::stop,
            theme.code_red,
            rv_editor_text("widgets_status.button_stop"),
            rv_editor_text("widgets_status.shortcut_stop"),
            state.stop,
            &out.stop },
        reload_slot,
    };
    const size_t shown = std::size(buttons); // Reload/Restart is always shown, disabled when it cannot act
    // One row, never a second: what does not fit goes behind a labelled More,
    // Reload first, then Step, then Build, which the menus also have; the rest keep their order.
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    float width = 0.0f;
    for (size_t i = 0; i < shown; ++i) {
        width += (i == 0 ? 0.0f : gap) + rv_editor_tool_button_width(buttons[i].label);
    }
    const float tail = reserve > 0.0f ? gap + reserve : 0.0f; // room what follows the bar needs
    bool hidden[std::size(buttons)] = {};
    bool any_hidden = false;
    if (width + tail > ImGui::GetContentRegionAvail().x) {
        width += gap + rv_editor_tool_button_width(rv_editor_text("widgets_status.button_more"));
        constexpr size_t drop_order[] = { 5, 3, 0, 2, 4, 1 };
        for (const size_t i : drop_order) {
            if (i >= shown || width + tail <= ImGui::GetContentRegionAvail().x) {
                continue;
            }
            hidden[i] = true;
            any_hidden = true;
            width -= gap + rv_editor_tool_button_width(buttons[i].label);
        }
    }
    bool first = true;
    for (size_t i = 0; i < shown; ++i) {
        const auto &b = buttons[i];
        if (hidden[i]) {
            continue;
        }
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        *b.clicked =
            rv_editor_tool_button(b.label, b.code, b.color, b.name, b.shortcut, theme, { rv_editor_look::live, b.disabled });
    }
    if (!any_hidden) {
        return out;
    }
    if (!first) {
        ImGui::SameLine();
    }
    if (rv_editor_tool_button(rv_editor_text("widgets_status.button_more"),
            rv_editor_glyph::more,
            theme.text,
            rv_editor_text("widgets_status.tooltip_more"),
            nullptr,
            theme)) {
        ImGui::OpenPopup("##transport_more");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##transport_more")) {
        for (size_t i = 0; i < shown; ++i) {
            const auto &b = buttons[i];
            if (!hidden[i]) {
                continue;
            }
            *b.clicked = ImGui::MenuItem(b.name, b.shortcut, false, b.disabled == nullptr);
            if (b.disabled != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", b.disabled);
            }
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
    return out;
}

void rv_editor_ask_begin(const char *title, const rv_editor_theme &theme)
{
    ImGui::PushID(title);
    ImGui::BeginChild("##ask", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    rv_editor_pane_header(title, true, theme);
}

void rv_editor_ask_end()
{
    ImGui::EndChild();
    ImGui::PopID();
}

void rv_editor_status_bar(const char *const fields[], int count, const rv_editor_theme &theme)
{
    if (count <= 0) {
        return;
    }
    const float h = ImGui::GetFrameHeight();
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float gap = static_cast<float>(status_bar_gap_px * theme.scale);
    float rest = 0.0f;
    for (int i = 1; i < count; ++i) {
        rest += ImGui::CalcTextSize(fields[i]).x + pad * status_bar_field_pads + gap;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const ImVec2 text = ImGui::CalcTextSize(fields[i]);
        const float field_pad = pad * status_bar_field_pads;
        const float w = i == 0 ? std::max(width - rest, field_pad) : text.x + field_pad;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + h);
        rv_editor_draw_panel(dl, min, max, theme, theme.window, rv_editor_bevel::sunken);
        const float text_x = std::floor(min.x + pad * status_bar_text_inset_pads);
        const float text_y = std::floor((min.y + max.y - text.y) / half_divisor);
        dl->AddText(ImVec2(text_x, text_y), rv_editor_col(theme.text), fields[i]);
        x = max.x + gap;
    }
    ImGui::Dummy(ImVec2(width, h));
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
