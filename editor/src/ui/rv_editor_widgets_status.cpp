// Status indicators, log rows, the transport bar and dialogs.

#include <algorithm>
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
// The share of the bar each field after the first takes; the build field fits its longest text at 1280 px.
constexpr float status_bar_field_share = 0.26f;
// Theme pads added to each status field's text width.
constexpr float status_bar_field_pads = 4.0f;
// Theme pads from a field's left edge to its text.
constexpr float status_bar_text_inset_pads = 2.0f;
// Divisor for centering by halving.
constexpr float half_divisor = 2.0f;
// Tile sizes the transport bar tries, largest first: the first whose row fits the window wins.
constexpr rv_editor_tile_size transport_sizes[] = {
    rv_editor_tile_size::large,
    rv_editor_tile_size::medium,
    rv_editor_tile_size::small,
};

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
    // Run/Resume and Reload/Restart swap labels: a medium tile keeps the wider of the two, so the
    // size is chosen by the width alone and no state moves a button.
    const auto widest = [](rv_editor_tile a, const rv_editor_tile &b) {
        a.min_width = std::max(rv_editor_tile_width(a, rv_editor_tile_size::medium),
            rv_editor_tile_width(b, rv_editor_tile_size::medium));
        return a;
    };
    const rv_editor_tile run_tile{ rv_editor_text("widgets_status.button_run"), rv_editor_glyph::run };
    const rv_editor_tile resume_tile{ rv_editor_text("widgets_status.button_resume"), rv_editor_glyph::run };
    const rv_editor_tile reload_tile{ rv_editor_text("widgets_status.button_reload"), rv_editor_glyph::reload };
    const rv_editor_tile restart_tile{ rv_editor_text("widgets_status.button_restart"), rv_editor_glyph::restart };
    reload_slot.tile = widest(reload_slot.tile, state.build_restart ? reload_tile : restart_tile);
    const rv_editor_transport_button buttons[] = {
        { { rv_editor_text("widgets_status.button_build"),
              rv_editor_glyph::build,
              rv_editor_mocha_peach,
              rv_editor_text("widgets_status.button_build"),
              rv_editor_text("widgets_status.shortcut_build") },
            state.build,
            &out.build },
        { widest({ run_resume_label,
                     rv_editor_glyph::run,
                     theme.code_green,
                     run_resume_label,
                     rv_editor_text("widgets_status.shortcut_run") },
              state.resume ? run_tile : resume_tile),
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
    // Reload/Restart is always shown, disabled when it cannot act. The bar wraps only when no size fits
    // and never hides a button: it takes the largest size whose row,
    // with `reserve` after it, fits the window; if none fits, small is drawn and the
    // buttons that do not fit wrap to a second row.
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float tail = reserve > 0.0f ? gap + reserve : 0.0f; // room what follows the bar needs
    const float avail = ImGui::GetContentRegionAvail().x;
    rv_editor_tile_size size = rv_editor_tile_size::small;
    for (const rv_editor_tile_size candidate : transport_sizes) {
        float row = 0.0f;
        for (size_t i = 0; i < std::size(buttons); ++i) {
            row += (i == 0 ? 0.0f : gap) + rv_editor_tile_width(buttons[i].tile, candidate);
        }
        if (row + tail <= avail) {
            size = candidate;
            break;
        }
    }
    for (size_t i = 0; i < std::size(buttons); ++i) {
        const auto &b = buttons[i];
        if (i > 0) {
            // A row that fits is one row; when even small does not, the rest goes below instead of past the edge.
            rv_editor_flow(rv_editor_tile_width(b.tile, size));
        }
        *b.clicked = rv_editor_tile_button(b.tile, size, theme, { rv_editor_look::live, b.disabled });
    }
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

namespace
{

// Open flags of the ask places and overlays being drawn, innermost last.
std::vector<bool> &ask_open_stack()
{
    static std::vector<bool> stack;
    return stack;
}

// The ask place's height: border, padding, the header, `lines` of text and a button row, with the spacing between.
float ask_place_height(int lines)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    const float rows = static_cast<float>(std::max(lines, 1));
    // The header, each line and the button row are items: a gap after every one but the last.
    return 2.0f * (st.WindowPadding.y + st.ChildBorderSize) + 2.0f * ImGui::GetFrameHeight() +
        rows * ImGui::GetTextLineHeight() + (rows + 1.0f) * st.ItemSpacing.y;
}

} // namespace

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

bool rv_editor_ask_place_begin(const char *id, const char *title, int lines, bool open, const rv_editor_theme &theme)
{
    ask_open_stack().push_back(open);
    ImGui::PushID(id);
    const ImVec2 size(0.0f, ask_place_height(lines));
    if (open) {
        ImGui::BeginChild("##ask", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    } else {
        // Closed it is the same place unseen and untouchable, so what a caller draws in it anyway keeps its rects:
        // the content goes to a draw channel that is dropped at the end.
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::BeginDisabled();
        ImGui::BeginChild("##ask", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
        ImGui::GetWindowDrawList()->ChannelsSplit(2);
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
    }
    rv_editor_pane_header(title, true, theme);
    return open;
}

void rv_editor_ask_place_end()
{
    if (!ask_open_stack().back()) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->ChannelsSetCurrent(0);
        // shortcut: uses ImDrawListSplitter internals, recheck on an ImGui update
        dl->_Splitter._Channels[1]._CmdBuffer.resize(0);
        dl->_Splitter._Channels[1]._IdxBuffer.resize(0);
        dl->ChannelsMerge();
    }
    ImGui::EndChild();
    if (!ask_open_stack().back()) {
        ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
    }
    ask_open_stack().pop_back();
    ImGui::PopID();
}

bool rv_editor_ask_overlay_begin(const char *id,
    const char *title,
    ImVec2 min,
    ImVec2 max,
    bool open,
    const rv_editor_theme &theme)
{
    ask_open_stack().push_back(open);
    if (open) {
        ImGui::SetNextWindowPos(min);
        ImGui::SetNextWindowSize(ImVec2(max.x - min.x, max.y - min.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin(id,
            nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PopStyleVar();
        rv_editor_pane_header(title, true, theme);
    }
    return open;
}

void rv_editor_ask_overlay_end()
{
    if (ask_open_stack().back()) {
        ImGui::End();
    }
    ask_open_stack().pop_back();
}

void rv_editor_status_bar(const char *const fields[], int count, const rv_editor_theme &theme, rv_editor_status_probe *probe)
{
    if (count <= 0) {
        return;
    }
    const float h = ImGui::GetFrameHeight();
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float gap = static_cast<float>(status_bar_gap_px * theme.scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    // Every field but the first takes a fixed share of the bar, the first the rest: the edges never follow the texts.
    const float share = std::floor(width * status_bar_field_share);
    const float first = std::max(width - static_cast<float>(count - 1) * (share + gap), pad * status_bar_field_pads);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const float field_pad = pad * status_bar_field_pads;
        const float w = i == 0 ? first : share;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + h);
        rv_editor_draw_panel(dl, min, max, theme, theme.window, rv_editor_bevel::sunken);
        const std::string shown = rv_editor_ellipsize(fields[i], std::max(0.0f, w - field_pad));
        const ImVec2 text = ImGui::CalcTextSize(shown.c_str());
        const float text_x = std::floor(min.x + pad * status_bar_text_inset_pads);
        const float text_y = std::floor((min.y + max.y - text.y) / half_divisor);
        dl->AddText(ImVec2(text_x, text_y), rv_editor_col(theme.text), shown.c_str());
        const bool clipped = shown != fields[i];
        if (clipped && ImGui::IsMouseHoveringRect(min, max)) {
            ImGui::SetTooltip("%s", fields[i]);
        }
        if (probe != nullptr) {
            probe->segments.push_back({ min.x, max.x, shown, clipped ? fields[i] : "" });
        }
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
