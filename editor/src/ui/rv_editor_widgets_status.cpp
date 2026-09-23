// Status indicators, log rows, the transport bar and dialogs.

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
        return {"INF", t.text_disabled};
    case rv_editor_severity::warning:
        return {"WRN", t.warning};
    case rv_editor_severity::error:
        return {"ERR", t.error};
    }
    return {"???", t.text};
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

void rv_editor_log_row(const char *time, const char *source, rv_editor_severity severity, const char *text,
    const rv_editor_theme &theme)
{
    const rv_editor_mark mark = rv_editor_severity_mark(severity, theme);
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
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
    const struct
    {
        const char *label;
        const char *disabled;
        bool *clicked;
    } buttons[] = {
        {"Build", state.build, &out.build},
        {"Run", state.run, &out.run},
        {"Pause", state.pause, &out.pause},
        {"Step", state.step, &out.step},
        {"Stop", state.stop, &out.stop},
        {"Reload", state.reload, &out.reload},
    };
    bool first = true;
    for (const auto &b : buttons) {
        if (!first) {
            ImGui::SameLine(0.0f, static_cast<float>(theme.scale));
        }
        first = false;
        *b.clicked = rv_editor_button(b.label, theme, {rv_editor_look::live, b.disabled});
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

} // namespace rv_editor
