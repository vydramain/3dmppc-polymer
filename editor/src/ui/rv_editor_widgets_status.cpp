// Status indicators, log rows, the transport bar and dialogs.

#include <algorithm>
#include <cmath>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Every state has a symbol as well as a colour, so none is told by colour alone.
struct rv_editor_mark
{
    const char *symbol;
    uint32_t color;
};

rv_editor_mark rv_editor_status_mark(rv_editor_status_kind kind, const rv_editor_theme &t)
{
    switch (kind) {
    case rv_editor_status_kind::idle:
        return {"-", t.text_disabled};
    case rv_editor_status_kind::busy:
        return {"~", t.selection};
    case rv_editor_status_kind::ok:
        return {"+", t.ok};
    case rv_editor_status_kind::warning:
        return {"!", t.warning};
    case rv_editor_status_kind::error:
        return {"x", t.error};
    }
    return {"?", t.text};
}

rv_editor_mark rv_editor_severity_mark(rv_editor_severity severity, const rv_editor_theme &t)
{
    switch (severity) {
    case rv_editor_severity::info:
        return {"INF", t.code_blue};
    case rv_editor_severity::warning:
        return {"WRN", t.code_yellow};
    case rv_editor_severity::error:
        return {"ERR", t.code_red};
    }
    return {"???", t.code_text};
}

} // namespace

void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme)
{
    const rv_editor_mark mark = rv_editor_status_mark(kind, theme);
    const float box = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(box, box));

    // A sunken lamp holding the symbol in the state's colour.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + box, min.y + box);
    rv_editor_draw_panel(dl, min, max, theme, theme.dark, rv_editor_bevel::sunken);
    const ImVec2 size = ImGui::CalcTextSize(mark.symbol);
    dl->AddText(ImVec2(std::floor((min.x + max.x - size.x) / 2.0f), std::floor((min.y + max.y - size.y) / 2.0f)),
        rv_editor_col(mark.color), mark.symbol);

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
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
    ImGui::PopStyleColor(2);
}

void rv_editor_log_row(const char *time, const char *source, rv_editor_severity severity, const char *text,
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

rv_editor_transport_actions rv_editor_transport_bar(const rv_editor_transport_state &state,
    const rv_editor_theme &theme)
{
    rv_editor_transport_actions out = {};
    // Step and Stop differ in picture and colour, not in the label alone.
    const struct
    {
        const char *label;
        rv_editor_glyph glyph;
        uint32_t color;
        const char *shortcut;
        const char *disabled;
        bool *clicked;
    } buttons[] = {
        {"Build", rv_editor_glyph::build, 0xfab387, "Ctrl+B", state.build, &out.build},
        {state.resume ? "Resume" : "Run", rv_editor_glyph::run, theme.code_green, "F5", state.run, &out.run},
        {"Pause", rv_editor_glyph::pause, theme.code_yellow, "F6", state.pause, &out.pause},
        {"Step", rv_editor_glyph::step, theme.code_blue, "F7", state.step, &out.step},
        {"Stop", rv_editor_glyph::stop, theme.code_red, "Shift+F5", state.stop, &out.stop},
        {"Reload", rv_editor_glyph::reload, 0xcba6f7, nullptr, state.reload, &out.reload},
    };
    bool first = true;
    for (const auto &b : buttons) {
        if (!first) {
            rv_editor_flow(rv_editor_tool_button_width(b.label));
        }
        first = false;
        *b.clicked =
            rv_editor_tool_button(b.label, b.glyph, b.color, b.shortcut, theme, {rv_editor_look::live, b.disabled});
    }
    return out;
}

bool rv_editor_dialog_begin(const char *title, const rv_editor_theme &theme)
{
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::BeginPopupModal(title, nullptr, flags)) {
        return false;
    }
    rv_editor_pane_header(title, true, theme);
    return true;
}

void rv_editor_dialog_end()
{
    ImGui::EndPopup();
}

void rv_editor_status_bar(const char *const fields[], int count, const rv_editor_theme &theme)
{
    if (count <= 0) {
        return;
    }
    const float h = ImGui::GetFrameHeight();
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float gap = static_cast<float>(2 * theme.scale);
    float rest = 0.0f;
    for (int i = 1; i < count; ++i) {
        rest += ImGui::CalcTextSize(fields[i]).x + pad * 4.0f + gap;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const ImVec2 text = ImGui::CalcTextSize(fields[i]);
        const float w = i == 0 ? std::max(width - rest, pad * 4.0f) : text.x + pad * 4.0f;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + h);
        rv_editor_draw_panel(dl, min, max, theme, theme.window, rv_editor_bevel::sunken);
        const ImVec2 at(std::floor(min.x + pad * 2.0f), std::floor((min.y + max.y - text.y) / 2.0f));
        dl->AddText(at, rv_editor_col(theme.text), fields[i]);
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
