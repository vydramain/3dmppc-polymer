// The layout probe: draws a pane body in a fixed-size window and records the rectangle of every item the body names,
// of every child window and the content height, so a test can state "nothing appears, moves or resizes by state; the
// tile relayouts only when it is resized".
// Every item and window is recorded automatically through ImGui's test-engine item hook (ui/rv_editor_item_hook.hpp),
// because tests cannot add calls into the panes drawn from editor/src; probe.item only adds an optional label.

#pragma once

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test_imgui.hpp"
#include "ui/rv_editor_item_hook.hpp"

namespace rv_editor_test
{

struct rv_editor_probe_entry {
    ImGuiID id = 0;
    std::string label; // the item's debug label (a window's name for a window), else empty
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float text_w = 0, text_h = 0; // the size of the label's text without its "##" part
    bool in_scroll = false;       // inside a window opened through rv_editor_scroll_begin
    bool scroll_area = false;     // a window opened through rv_editor_scroll_begin
    bool scrollbar = false;       // a window that shows a scroll bar
};

struct rv_editor_probe_record {
    std::vector<rv_editor_probe_entry> entries; // in submission order
    float content_height = 0;

    // Optional: names the last submitted item, for items ImGui gives no label.
    void item(const char *label)
    {
        if (!entries.empty()) {
            entries.back().label = label;
        }
    }
};

namespace rv_editor_probe_detail
{

inline std::string name(const rv_editor_probe_entry &e)
{
    return e.label.empty() ? "id " + std::to_string(e.id) : "'" + e.label + "'";
}

inline std::string rect(float x0, float y0, float x1, float y1)
{
    return "(" + std::to_string(x0) + "," + std::to_string(y0) + ")-(" + std::to_string(x1) + "," + std::to_string(y1) + ")";
}

inline std::string rect(const rv_editor_probe_entry &e)
{
    return rect(e.x0, e.y0, e.x1, e.y1);
}

struct sink final : rv_editor::rv_editor_item_sink {
    rv_editor_probe_record *record;
    std::set<ImGuiID> areas; // windows opened through rv_editor_scroll_begin
    explicit sink(rv_editor_probe_record *r)
        : record(r)
    {
    }
    void item_add(ImGuiID id, ImVec2 min, ImVec2 max) override
    {
        // NewFrame opens ImGui's implicit "Debug" window while the hook is already on; it is not the pane.
        if (std::strcmp(ImGui::GetCurrentContext()->CurrentWindow->Name, "Debug##Default") == 0) {
            return;
        }
        rv_editor_probe_entry e{ id, "", min.x, min.y, max.x, max.y };
        for (const ImGuiWindow *w = ImGui::GetCurrentContext()->CurrentWindow; w != nullptr; w = w->ParentWindow) {
            e.in_scroll = e.in_scroll || areas.count(w->ID) != 0;
        }
        record->entries.push_back(e);
    }
    void scroll_area(ImGuiID window) override
    {
        areas.insert(window);
    }
    void item_info(ImGuiID id, const char *label) override
    {
        for (auto it = record->entries.rbegin(); it != record->entries.rend(); ++it) {
            if (it->id == id) {
                it->label = label;
                // A window's name is not drawn text; a title bar takes its name, so only items with a visible label count.
                const ImGuiContext *g = ImGui::GetCurrentContext();
                if (ImGui::FindWindowByID(id) == nullptr && g->CurrentWindow->MoveId != id) {
                    const char *hidden = std::strstr(label, "##");
                    const ImVec2 size = ImGui::CalcTextSize(label, hidden != nullptr ? hidden : nullptr);
                    it->text_w = size.x;
                    it->text_h = size.y;
                }
                return;
            }
        }
    }
};

} // namespace rv_editor_probe_detail

// "" when the records are equal, else the first difference: the label and both rectangles (or the heights).
inline std::string rv_editor_probe_compare(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    using namespace rv_editor_probe_detail;
    std::vector<bool> used(b.entries.size(), false);
    for (const auto &ea : a.entries) {
        size_t k = 0;
        // Match by id; items without one (id 0) match in order.
        while (k < b.entries.size() && (used[k] || b.entries[k].id != ea.id)) {
            ++k;
        }
        if (k == b.entries.size()) {
            return name(ea) + " " + rect(ea) + " is missing from the second draw";
        }
        used[k] = true;
        const auto &eb = b.entries[k];
        if (ea.x0 != eb.x0 || ea.y0 != eb.y0 || ea.x1 != eb.x1 || ea.y1 != eb.y1) {
            return name(ea) + " " + rect(ea) + " became " + rect(eb);
        }
    }
    for (size_t k = 0; k < b.entries.size(); ++k) {
        if (!used[k]) {
            return name(b.entries[k]) + " " + rect(b.entries[k]) + " is missing from the first draw";
        }
    }
    if (a.content_height != b.content_height) {
        return "content height " + std::to_string(a.content_height) + " became " + std::to_string(b.content_height);
    }
    return "";
}

// One frame at `width` x `height`: runs body(record) in the window and returns what was recorded.
template <class F>
rv_editor_probe_record rv_editor_probe_draw(rv_editor_test_imgui &gui, float width, F body, float height = 300)
{
    rv_editor_probe_record record;
    rv_editor_probe_detail::sink sink(&record);
    rv_editor::rv_editor_item_sink_set(&sink);
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    gui.frame(
        [&] {
            body(record);
            record.content_height = ImGui::GetCursorPosY();
        },
        width,
        height);
    ImGui::GetCurrentContext()->TestEngineHookItems = false;
    rv_editor::rv_editor_item_sink_set(nullptr);
    // The bars of this frame are known only after the window has ended.
    for (auto &e : record.entries) {
        const ImGuiWindow *w = ImGui::FindWindowByID(e.id);
        e.scroll_area = sink.areas.count(e.id) != 0;
        e.scrollbar = w != nullptr && (w->ScrollbarX || w->ScrollbarY);
    }
    return record;
}

struct rv_editor_probe_resize_result {
    rv_editor_probe_record first, third;
    std::string difference; // compare(first, third)
};

// Draws at w1, w2, w1 and compares the first and the third run.
template <class F>
rv_editor_probe_resize_result rv_editor_probe_resize(rv_editor_test_imgui &gui, float w1, float w2, F body)
{
    rv_editor_probe_resize_result r;
    r.first = rv_editor_probe_draw(gui, w1, body);
    rv_editor_probe_draw(gui, w2, body);
    r.third = rv_editor_probe_draw(gui, w1, body);
    r.difference = rv_editor_probe_compare(r.first, r.third);
    return r;
}

} // namespace rv_editor_test
