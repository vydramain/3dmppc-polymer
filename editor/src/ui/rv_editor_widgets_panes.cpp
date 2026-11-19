// Splitters and pane headers: the pieces the tiled workspace is built from.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui_internal.h" // whether the strip shows its scrollbar
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

bool rv_editor_splitter(const char *id,
    rv_editor_axis axis,
    float length,
    float *a,
    float *b,
    float min_a,
    float min_b,
    const rv_editor_theme &theme,
    const rv_editor_state &state)
{
    const float thickness = static_cast<float>(theme.pad_px * theme.scale);
    const bool across_x = axis == rv_editor_axis::x;
    const ImVec2 size = across_x ? ImVec2(thickness, length) : ImVec2(length, thickness);
    const rv_editor_item item = rv_editor_item_add(id, size, state);

    bool changed = false;
    if (item.held && state.look == rv_editor_look::live) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        // Move the boundary, but never past either side's minimum.
        float d = across_x ? delta.x : delta.y;
        d = std::max(d, min_a - *a);
        d = std::min(d, *b - min_b);
        if (d != 0.0f) {
            *a += d;
            *b -= d;
            changed = true;
        }
    }
    if ((item.hovered || item.held) && !item.disabled) {
        ImGui::SetMouseCursor(across_x ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, item.min, item.max, theme, item.held ? theme.selection : theme.window, rv_editor_bevel::raised);
    if (item.hovered && !item.held && !item.disabled) {
        rv_editor_draw_frame(dl, item.min, item.max, theme, theme.selection);
    }
    return changed;
}

namespace
{

// The tab side leans by half the frame height.
constexpr float tab_slant_height_divisor = 2.0f;
// Theme pads added to a tab's label width.
constexpr float tab_pad_count = 4.0f;
// Halves the free space to center text in its box.
constexpr float text_center_divisor = 2.0f;
// How many tab heights one wheel notch scrolls the strip.
constexpr float tab_wheel_step_heights = 3.0f;
// The `<`, `>` and `v` buttons of an overflowing strip, each a frame high and wide.
constexpr float tab_buttons = 3.0f;
// Theme pads kept free right of the title patch for the boxes.
constexpr float title_reserve_pads = 3.0f;
// Theme pads around the title text in its dark patch.
constexpr float title_patch_pads = 2.0f;

// One folder tab: a parallelogram leaning right, drawn a scaled pixel row at a
// time so its sides stay stepped and unsmoothed. Raised: light left edge, dark
// right edge; sunken swaps them.
void rv_editor_draw_tab(ImDrawList *dl,
    ImVec2 min,
    ImVec2 max,
    float slant,
    const rv_editor_theme &theme,
    uint32_t fill,
    bool raised)
{
    const float px = static_cast<float>(theme.scale);
    const float h = max.y - min.y;
    const ImU32 light = rv_editor_col(raised ? theme.bevel_hi : theme.bevel_lo);
    const ImU32 dark = rv_editor_col(raised ? theme.bevel_lo : theme.bevel_hi);
    for (float y = min.y; y < max.y; y += px) {
        const float shift = std::floor((y - min.y) / h * slant / px) * px;
        const float left = min.x + shift;
        const float right = max.x - slant + shift;
        dl->AddRectFilled(ImVec2(left, y), ImVec2(right, y + px), rv_editor_col(fill));
        dl->AddRectFilled(ImVec2(left, y), ImVec2(left + px, y + px), light);
        dl->AddRectFilled(ImVec2(right - px, y), ImVec2(right, y + px), dark);
    }
    dl->AddRectFilled(ImVec2(min.x + slant, max.y - px), ImVec2(max.x, max.y), dark);
}

// One letter box of a pane header: a small button face with hover, press and focus.
bool rv_editor_header_box(const char *id,
    const char *letter,
    ImVec2 pos,
    float size,
    const rv_editor_theme &theme,
    const rv_editor_state &state)
{
    ImGui::SetCursorScreenPos(pos);
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(size, size), state);
    const bool down = item.held && item.hovered;

    // Dark outline, then the bevel inside it, as on every push button.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float px = static_cast<float>(theme.scale);
    const ImVec2 min(item.min.x + px, item.min.y + px);
    const ImVec2 max(item.max.x - px, item.max.y - px);
    dl->AddRectFilled(item.min, item.max, rv_editor_col(theme.dark));
    rv_editor_draw_panel(dl,
        min,
        max,
        theme,
        down ? theme.inset : theme.button,
        down ? rv_editor_bevel::sunken : rv_editor_bevel::raised);
    if (item.hovered && !item.disabled) {
        rv_editor_draw_frame(dl, min, max, theme, theme.selection);
    }
    if (item.focused) {
        rv_editor_draw_focus(dl, min, max, theme);
    }
    const ImVec2 text = ImGui::CalcTextSize(letter);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const ImVec2 at(std::floor((item.min.x + item.max.x - text.x) / text_center_divisor + nudge),
        std::floor((item.min.y + item.max.y - text.y) / text_center_divisor + nudge));
    dl->AddText(at, rv_editor_col(theme.text), letter);
    return item.clicked;
}

} // namespace

bool rv_editor_tab_strip(const char *id,
    const char *const labels[],
    int count,
    int *active,
    const rv_editor_theme &theme,
    const rv_editor_state &state,
    int *pressed,
    float /*max_tab_width*/,
    rv_editor_tab_probe *probe)
{
    const float h = ImGui::GetFrameHeight();
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float slant = std::floor(h / tab_slant_height_divisor);
    // A tab is as wide as its whole label.
    std::vector<float> width(static_cast<size_t>(count));
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        width[static_cast<size_t>(i)] =
            ImGui::CalcTextSize(labels[i], rv_editor_label_end(labels[i])).x + pad * tab_pad_count + slant;
        total += width[static_cast<size_t>(i)];
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool overflow = total > avail;
    // `<` before the strip, `>` and `v` after it; none while the tabs fit.
    const float strip_w = overflow ? std::max(0.0f, avail - tab_buttons * h) : avail;
    const float strip_x = origin.x + (overflow ? h : 0.0f);
    const float step = h * tab_wheel_step_heights;
    float scroll_by = 0.0f;
    bool changed = false;

    ImGui::PushID(id);
    if (probe != nullptr) {
        *probe = {};
    }
    if (overflow) {
        const float right = origin.x + avail;
        const ImVec2 prev_at(origin.x, origin.y);
        const ImVec2 next_at(right - 2.0f * h, origin.y);
        const ImVec2 list_at(right - h, origin.y);
        if (rv_editor_header_box("##prev", "<", prev_at, h, theme, state)) {
            scroll_by -= step;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", rv_editor_text("widgets_tabs.prev"));
        }
        if (rv_editor_header_box("##next", ">", next_at, h, theme, state)) {
            scroll_by += step;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", rv_editor_text("widgets_tabs.next"));
        }
        if (rv_editor_header_box("##list", "v", list_at, h, theme, state)) {
            ImGui::OpenPopup("##tablist");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", rv_editor_text("widgets_tabs.list"));
        }
        if (probe != nullptr) {
            probe->prev = { prev_at.x, prev_at.y, prev_at.x + h, prev_at.y + h };
            probe->next = { next_at.x, next_at.y, next_at.x + h, next_at.y + h };
            probe->list = { list_at.x, list_at.y, list_at.x + h, list_at.y + h };
        }
        // Open away from the nearer screen edge: a tile's tab row is mostly at the bottom.
        const bool above = list_at.y > ImGui::GetIO().DisplaySize.y / text_center_divisor;
        ImGui::SetNextWindowPos(ImVec2(list_at.x + h, above ? list_at.y : list_at.y + h),
            ImGuiCond_Always,
            ImVec2(1.0f, above ? 1.0f : 0.0f));
        rv_editor_menu_style_push();
        if (ImGui::BeginPopup("##tablist")) {
            if (probe != nullptr) {
                probe->list_open = true;
            }
            for (int i = 0; i < count; ++i) {
                const std::string whole(labels[i], rv_editor_label_end(labels[i]));
                ImGui::PushID(i);
                if (ImGui::Selectable(whole.c_str(), i == *active) && *active != i) {
                    *active = i;
                    changed = true;
                }
                ImGui::PopID();
                if (probe != nullptr) {
                    probe->list_items.push_back({ ImGui::GetItemRectMin().x,
                        ImGui::GetItemRectMin().y,
                        ImGui::GetItemRectMax().x,
                        ImGui::GetItemRectMax().y });
                }
            }
            ImGui::EndPopup();
        }
        rv_editor_menu_style_pop();
    }

    ImGui::SetCursorScreenPos(ImVec2(strip_x, origin.y));
    // No scrollbar and no room for one: the arrows and the list replace it.
    ImGui::BeginChild("##strip",
        ImVec2(strip_w, h),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    const float base = ImGui::GetCursorScreenPos().x; // content x = 0, scrolled
    float scroll = ImGui::GetScrollX();
    if (overflow && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        scroll -= ImGui::GetIO().MouseWheel * step;
    }
    scroll += scroll_by;
    // An active tab that changed (a click, the list, the owner) is scrolled in until it is; a frame too early for the
    // content size to be known keeps asking.
    ImGuiStorage *store = ImGui::GetStateStorage();
    const ImGuiID last_id = ImGui::GetID("##last_active");
    if (count > 0 && store->GetInt(last_id, -1) != *active && *active >= 0 && *active < count) {
        float x0 = 0.0f;
        for (int i = 0; i < *active; ++i) {
            x0 += width[static_cast<size_t>(i)];
        }
        const float x1 = x0 + width[static_cast<size_t>(*active)];
        float want = scroll;
        if (x1 > want + strip_w) {
            want = x1 - strip_w;
        }
        if (x0 < want) {
            want = x0;
        }
        want = std::clamp(want, 0.0f, std::max(0.0f, total - strip_w));
        if (std::fabs(want - ImGui::GetScrollX()) < 1.0f) {
            store->SetInt(last_id, *active);
        }
        scroll = want;
    }
    if (scroll != ImGui::GetScrollX()) {
        ImGui::SetScrollX(std::clamp(scroll, 0.0f, std::max(0.0f, total - strip_w)));
    }
    if (probe != nullptr) {
        probe->strip_x0 = ImGui::GetWindowPos().x;
        probe->strip_x1 = probe->strip_x0 + ImGui::GetWindowWidth();
        probe->strip_y0 = ImGui::GetWindowPos().y;
        probe->strip_y1 = probe->strip_y0 + ImGui::GetWindowHeight();
        probe->scroll_x = ImGui::GetScrollX();
        probe->scrollbar = ImGui::GetCurrentWindow()->ScrollbarX;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float top = origin.y;
    float x = base;
    for (int i = 0; i < count; ++i) {
        const float w = width[static_cast<size_t>(i)];
        const std::string whole(labels[i], rv_editor_label_end(labels[i]));
        rv_editor_tab_probe::rect box{ x, top, x + w, top + h };
        // A tab wholly outside the strip is no item: it is neither drawn nor clicked, and the tile has nothing of it.
        if (x + w > strip_x && x < strip_x + strip_w) {
            const ImVec2 text = ImGui::CalcTextSize(whole.c_str());
            // The item is the visible part, so nothing of the tile lies outside the tile; the tab is drawn whole.
            const float seen_x0 = std::max(x, strip_x);
            ImGui::SetCursorScreenPos(ImVec2(seen_x0, top));
            ImGui::PushID(i);
            rv_editor_item item = rv_editor_item_add("##tab", ImVec2(std::min(x + w, strip_x + strip_w) - seen_x0, h), state);
            ImGui::PopID();
            item.min = ImVec2(x, top);
            item.max = ImVec2(x + w, top + h);
            if (pressed != nullptr && item.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                *pressed = i;
            }
            if (item.clicked && *active != i) {
                *active = i;
                changed = true;
            }
            const bool front = i == *active;
            const bool down = item.held && item.hovered;
            uint32_t fill = front ? theme.selection : theme.button;
            if (!front && item.hovered && !item.disabled) {
                fill = theme.bevel_hi;
            }
            rv_editor_draw_tab(dl, item.min, item.max, slant, theme, fill, !down);
            const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
            const ImVec2 at(std::floor((item.min.x + item.max.x - text.x) / text_center_divisor + nudge),
                std::floor((item.min.y + item.max.y - text.y) / text_center_divisor + nudge));
            const uint32_t ink = item.disabled ? theme.text_disabled : (front ? theme.dark : theme.text);
            dl->AddText(at, rv_editor_col(ink), whole.c_str());
            if (item.focused) {
                rv_editor_draw_focus(dl, ImVec2(at.x - pad, item.min.y), ImVec2(at.x + text.x + pad, item.max.y), theme);
            }
        }
        if (probe != nullptr) {
            probe->tabs.push_back(box);
            probe->shown.push_back(whole);
        }
        x += w;
    }
    // The strip's extent for the scroll range, without an item.
    ImGui::SetCursorScreenPos(ImVec2(base + total, top));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(avail, h));
    return changed;
}

float rv_editor_tab_strip_height()
{
    return ImGui::GetFrameHeight();
}

float rv_editor_pane_header_min_width(const char *title, const rv_editor_theme &theme)
{
    const float bevel = static_cast<float>(theme.bevel_px * theme.scale);
    const float box = ImGui::GetFrameHeight() - 2.0f * bevel;
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float text = ImGui::CalcTextSize(title, rv_editor_label_end(title)).x;
    return std::ceil(text + 2.0f * box + pad * (1.0f + title_reserve_pads) + 2.0f * bevel);
}

rv_editor_header_action rv_editor_pane_header(const char *title,
    bool active,
    const rv_editor_theme &theme,
    bool controls,
    const rv_editor_state &state,
    bool *title_pressed)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + h);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, min, max, theme, theme.dark, rv_editor_bevel::raised);
    const float bevel = static_cast<float>(theme.bevel_px * theme.scale);
    const ImVec2 inner_min(min.x + bevel, min.y + bevel);
    const ImVec2 inner_max(max.x - bevel, max.y - bevel);
    rv_editor_draw_stipple(dl, inner_min, inner_max, theme, active ? theme.bevel_hi : theme.button);

    rv_editor_header_action action = rv_editor_header_action::none;
    float title_x = inner_min.x;
    float title_limit = inner_max.x;
    if (controls) {
        const float box = inner_max.y - inner_min.y;
        ImGui::PushID(title);
        if (rv_editor_header_box("##close", "X", inner_min, box, theme, state)) {
            action = rv_editor_header_action::close;
        }
        const ImVec2 right(inner_max.x - box, inner_min.y);
        if (rv_editor_header_box("##maximize", "M", right, box, theme, state)) {
            action = rv_editor_header_action::maximize;
        }
        ImGui::PopID();
        title_x += box;
        title_limit -= box;
    }

    // Where a header drag may start: the title area, not the X/M boxes.
    if (title_pressed != nullptr) {
        *title_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(ImVec2(title_x, inner_min.y), ImVec2(title_limit, inner_max.y));
    }

    // The title sits on a solid patch so the stipple never runs through letters.
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    // A title longer than the room between the boxes ends in "..." and shows whole
    // in a tooltip.
    const char *end = rv_editor_label_end(title);
    const ImVec2 patch_min(title_x + pad, inner_min.y);
    const float ellipsis_space = title_limit - patch_min.x - pad * title_reserve_pads;
    const std::string shown = rv_editor_pane_header_title(title, std::max(0.0f, ellipsis_space));
    const bool elided = shown != std::string(title, end);
    const float text_w = ImGui::CalcTextSize(shown.c_str()).x;
    const float text_h = ImGui::GetFontSize();
    const ImVec2 patch_max(patch_min.x + text_w + pad * title_patch_pads, inner_max.y);
    dl->AddRectFilled(patch_min, patch_max, rv_editor_col(theme.dark));
    const ImVec2 pos(std::floor(patch_min.x + pad), std::floor((min.y + max.y - text_h) / text_center_divisor));
    const ImU32 ink = rv_editor_col(active ? theme.text_bright : theme.text);
    dl->AddText(pos, ink, shown.c_str());
    if (elided && ImGui::IsMouseHoveringRect(patch_min, patch_max)) {
        ImGui::SetTooltip("%.*s", static_cast<int>(end - title), title);
    }

    // The bar as one layout item, so what follows starts below it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(max.x - min.x, h));
    return action;
}

} // namespace rv_editor
