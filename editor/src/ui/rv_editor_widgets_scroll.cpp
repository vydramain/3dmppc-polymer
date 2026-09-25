// Motif scroll areas: a child window without ImGui's own scrollbars, and the
// editor's arrow scrollbars drawn beside it (UI-02).

#include <algorithm>
#include <cmath>
#include <vector>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_scroll_frame
{
    ImGuiID content_w; // storage keys: the content's size, and a scroll to apply
    ImGuiID content_h;
    ImGuiID want_x;
    ImGuiID want_y;
    ImVec2 min;     // the whole area, bars included
    ImVec2 max;
    bool horizontal; // a horizontal bar was asked for
    bool show_v;
    bool show_h;
    float bar;
};

// Begin/end nest like BeginChild/EndChild.
std::vector<rv_editor_scroll_frame> rv_editor_scroll_stack;

// Last frame's overflow and this frame's wanted scroll, kept by area id.
ImGuiStorage &rv_editor_scroll_store()
{
    return *ImGui::GetStateStorage();
}

// One scrollbar along `axis` in [min, max): arrow boxes at both ends, a sunken
// trough, a raised thumb with a grip. Returns the new scroll value.
float rv_editor_scrollbar(const char *id, bool vertical, ImVec2 min, ImVec2 max, float scroll, float scroll_max,
    float view, const rv_editor_theme &theme)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float bar = vertical ? max.x - min.x : max.y - min.y;
    const float length = vertical ? max.y - min.y : max.x - min.x;
    const float step = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    ImGui::PushID(id);

    // The arrow boxes.
    auto arrow = [&](const char *box, ImVec2 a, ImVec2 b, ImGuiDir dir) {
        ImGui::SetCursorScreenPos(a);
        const rv_editor_item item = rv_editor_item_add(box, ImVec2(b.x - a.x, b.y - a.y), {});
        const bool down = item.held && item.hovered;
        rv_editor_draw_panel(dl, a, b, theme, down ? theme.inset : theme.button,
            down ? rv_editor_bevel::sunken : rv_editor_bevel::raised);
        const float inset = std::floor(bar / 4.0f);
        rv_editor_draw_arrow(dl, ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), theme, dir,
            item.hovered ? theme.text_bright : theme.text);
        // Held down, the arrow keeps scrolling, like a Motif arrow button.
        return item.clicked || (item.held && ImGui::GetIO().MouseDownDuration[0] > 0.3f);
    };
    const ImVec2 first_max = vertical ? ImVec2(max.x, min.y + bar) : ImVec2(min.x + bar, max.y);
    const ImVec2 last_min = vertical ? ImVec2(min.x, max.y - bar) : ImVec2(max.x - bar, min.y);
    if (arrow("##less", min, first_max, vertical ? ImGuiDir_Up : ImGuiDir_Left)) {
        scroll -= step * (ImGui::GetIO().MouseDownDuration[0] > 0.3f ? ImGui::GetIO().DeltaTime * 10.0f : 1.0f);
    }
    if (arrow("##more", last_min, max, vertical ? ImGuiDir_Down : ImGuiDir_Right)) {
        scroll += step * (ImGui::GetIO().MouseDownDuration[0] > 0.3f ? ImGui::GetIO().DeltaTime * 10.0f : 1.0f);
    }

    // The trough and the thumb in it.
    const ImVec2 t_min = vertical ? ImVec2(min.x, first_max.y) : ImVec2(first_max.x, min.y);
    const ImVec2 t_max = vertical ? ImVec2(max.x, last_min.y) : ImVec2(last_min.x, max.y);
    const float track = std::max(0.0f, length - 2.0f * bar);
    rv_editor_draw_panel(dl, t_min, t_max, theme, theme.inset, rv_editor_bevel::sunken);
    ImGui::SetCursorScreenPos(t_min);
    const rv_editor_item trough = rv_editor_item_add("##track", ImVec2(t_max.x - t_min.x, t_max.y - t_min.y), {});

    const float content = view + scroll_max;
    const float thumb = std::clamp(content > 0.0f ? track * view / content : track, std::min(bar, track), track);
    const float room = track - thumb;
    const float at = scroll_max > 0.0f ? room * std::clamp(scroll / scroll_max, 0.0f, 1.0f) : 0.0f;
    const ImVec2 th_min = vertical ? ImVec2(t_min.x, std::floor(t_min.y + at)) : ImVec2(std::floor(t_min.x + at), t_min.y);
    const ImVec2 th_max = vertical ? ImVec2(t_max.x, std::floor(th_min.y + thumb)) : ImVec2(std::floor(th_min.x + thumb), t_max.y);

    if (trough.held && room > 0.0f) {
        // While the trough is held the thumb's middle follows the pointer.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float p = (vertical ? mouse.y - t_min.y : mouse.x - t_min.x) - thumb / 2.0f;
        scroll = scroll_max * std::clamp(p / room, 0.0f, 1.0f);
    }
    const bool hot = trough.hovered || trough.held;
    rv_editor_draw_panel(dl, th_min, th_max, theme, hot ? theme.bevel_hi : theme.button, rv_editor_bevel::raised);
    // The grip: three grooves across the thumb's middle.
    const float px = static_cast<float>(theme.scale);
    const ImVec2 mid((th_min.x + th_max.x) / 2.0f, (th_min.y + th_max.y) / 2.0f);
    for (int k = -1; k <= 1; ++k) {
        const float o = std::floor(static_cast<float>(k) * 3.0f * px);
        if (vertical) {
            const float y = std::floor(mid.y + o);
            dl->AddRectFilled(ImVec2(th_min.x + 3 * px, y), ImVec2(th_max.x - 3 * px, y + px), rv_editor_col(theme.bevel_lo));
            dl->AddRectFilled(ImVec2(th_min.x + 3 * px, y + px), ImVec2(th_max.x - 3 * px, y + 2 * px), rv_editor_col(theme.bevel_hi));
        } else {
            const float x = std::floor(mid.x + o);
            dl->AddRectFilled(ImVec2(x, th_min.y + 3 * px), ImVec2(x + px, th_max.y - 3 * px), rv_editor_col(theme.bevel_lo));
            dl->AddRectFilled(ImVec2(x + px, th_min.y + 3 * px), ImVec2(x + 2 * px, th_max.y - 3 * px), rv_editor_col(theme.bevel_hi));
        }
    }
    ImGui::PopID();
    return std::clamp(scroll, 0.0f, scroll_max);
}

} // namespace

bool rv_editor_scroll_begin(const char *id, ImVec2 size, bool horizontal, ImGuiChildFlags child_flags)
{
    ImGui::PushID(id);
    ImGuiStorage &store = rv_editor_scroll_store();
    rv_editor_scroll_frame f{};
    f.content_w = ImGui::GetID("##content_w");
    f.content_h = ImGui::GetID("##content_h");
    f.want_x = ImGui::GetID("##want_x");
    f.want_y = ImGui::GetID("##want_y");
    f.horizontal = horizontal;
    // In step with the interface text, not with a button's height.
    f.bar = std::floor(ImGui::GetTextLineHeight() * 1.25f);
    f.min = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // As for BeginChild: 0 takes what is left, a negative size leaves that much.
    f.max = ImVec2(f.min.x + (size.x > 0.0f ? size.x : avail.x + size.x),
        f.min.y + (size.y > 0.0f ? size.y : avail.y + size.y));
    // Bars from the content's size against the whole area. ImGui's own ScrollMax
    // compares last frame's content with this frame's view, so deciding from it
    // makes one bar's width summon the other bar and the two blink in turn.
    const float cw = store.GetFloat(f.content_w, 0.0f);
    const float ch = store.GetFloat(f.content_h, 0.0f);
    const float aw = f.max.x - f.min.x;
    const float ah = f.max.y - f.min.y;
    const bool h0 = horizontal && cw > aw + 0.5f;
    f.show_v = ch > ah + 0.5f || (h0 && ch > ah - f.bar + 0.5f);
    f.show_h = h0 || (horizontal && f.show_v && cw > aw - f.bar + 0.5f);

    const float want_x = store.GetFloat(f.want_x, -1.0f);
    const float want_y = store.GetFloat(f.want_y, -1.0f);
    if (want_x >= 0.0f || want_y >= 0.0f) {
        ImGui::SetNextWindowScroll(ImVec2(want_x, want_y));
        store.SetFloat(f.want_x, -1.0f);
        store.SetFloat(f.want_y, -1.0f);
    }
    const ImVec2 inner(f.max.x - f.min.x - (f.show_v ? f.bar : 0.0f), f.max.y - f.min.y - (f.show_h ? f.bar : 0.0f));
    // NoScrollbar hides ImGui's bars; HorizontalScrollbar is still needed for the
    // window to scroll along x at all, ours or the wheel's.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
    if (horizontal) {
        flags |= ImGuiWindowFlags_HorizontalScrollbar;
    }
    rv_editor_scroll_stack.push_back(f);
    return ImGui::BeginChild("##scroll", ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y)), child_flags, flags);
}

void rv_editor_scroll_end(const rv_editor_theme &theme)
{
    const rv_editor_scroll_frame f = rv_editor_scroll_stack.back();
    rv_editor_scroll_stack.pop_back();
    const float sx = ImGui::GetScrollX();
    const float sy = ImGui::GetScrollY();
    const float mx = ImGui::GetScrollMaxX();
    const float my = ImGui::GetScrollMaxY();
    const ImVec2 view = ImGui::GetWindowSize();
    ImGui::EndChild();

    ImGuiStorage &store = rv_editor_scroll_store();
    store.SetFloat(f.content_w, mx + view.x);
    store.SetFloat(f.content_h, my + view.y);
    if (f.show_v) {
        const ImVec2 a(f.max.x - f.bar, f.min.y);
        const ImVec2 b(f.max.x, f.max.y - (f.show_h ? f.bar : 0.0f));
        const float y = rv_editor_scrollbar("##v", true, a, b, sy, my, view.y, theme);
        if (y != sy) {
            store.SetFloat(f.want_y, y);
        }
    }
    if (f.show_h) {
        const ImVec2 a(f.min.x, f.max.y - f.bar);
        const ImVec2 b(f.max.x - (f.show_v ? f.bar : 0.0f), f.max.y);
        const float x = rv_editor_scrollbar("##h", false, a, b, sx, mx, view.x, theme);
        if (x != sx) {
            store.SetFloat(f.want_x, x);
        }
    }
    if (f.show_v && f.show_h) {
        rv_editor_draw_panel(ImGui::GetWindowDrawList(), ImVec2(f.max.x - f.bar, f.max.y - f.bar), f.max, theme,
            theme.window, rv_editor_bevel::raised);
    }
    // The whole area as one item, bars included, so the window around it sees
    // exactly its size and what follows starts under it, as after a child.
    ImGui::SetCursorScreenPos(f.min);
    ImGui::Dummy(ImVec2(f.max.x - f.min.x, f.max.y - f.min.y));
    ImGui::PopID();
}

} // namespace rv_editor
