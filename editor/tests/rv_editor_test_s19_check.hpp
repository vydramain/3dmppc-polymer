// Checks of the S19 tests over a probe record: items over each other, what moves between states, the states of a view at a
// size, and finders and a click.

#pragma once

#include "rv_editor_test_s19.hpp"

namespace rv_editor_test
{

namespace s19
{

// A window of the current ImGui frame by its record id.
inline bool is_window(const rv_editor_probe_entry &e)
{
    return ImGui::FindWindowByID(e.id) != nullptr;
}

// "" when no two items of the record that are not windows and not inside a scroll area overlap without one holding the
// other; else the first pair. A label that crosses its field is such an overlap.
inline std::string overlap(const rv_editor_probe_record &r)
{
    using namespace rv_editor_probe_detail;
    constexpr float eps = 0.5f;
    const auto holds = [](const rv_editor_probe_entry &a, const rv_editor_probe_entry &b) {
        return a.x0 <= b.x0 + eps && a.y0 <= b.y0 + eps && a.x1 >= b.x1 - eps && a.y1 >= b.y1 - eps;
    };
    for (size_t i = 0; i < r.entries.size(); ++i) {
        const auto &a = r.entries[i];
        if (a.in_scroll || is_window(a) || a.x1 - a.x0 < 1.0f || a.y1 - a.y0 < 1.0f) {
            continue;
        }
        for (size_t j = i + 1; j < r.entries.size(); ++j) {
            const auto &b = r.entries[j];
            if (b.in_scroll || is_window(b) || b.x1 - b.x0 < 1.0f || b.y1 - b.y0 < 1.0f) {
                continue;
            }
            const bool cross = a.x0 < b.x1 - eps && b.x0 < a.x1 - eps && a.y0 < b.y1 - eps && b.y0 < a.y1 - eps;
            if (cross && !holds(a, b) && !holds(b, a)) {
                return name(a) + " " + rect(a) + " overlaps " + name(b) + " " + rect(b);
            }
        }
    }
    return "";
}

// A line of plain text in an area (an unlabelled item in a scroll) shares its id with the others and its area's scroll window
// may change height and width by a bar, so the windows are compared without those lines.
inline rv_editor_probe_record without_lines(const rv_editor_probe_record &r)
{
    rv_editor_probe_record out = r;
    out.entries.clear();
    for (const auto &e : r.entries) {
        if (!(e.label.empty() && e.in_scroll)) {
            out.entries.push_back(e);
        }
    }
    return out;
}

// "" when every item the two records share (by id) that is not a window and not inside a scroll area is at the same
// rectangle, and both hold the same frames and scroll windows at the same rectangles; else the first difference.
inline std::string moved(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    using namespace rv_editor_probe_detail;
    for (const auto &ea : a.entries) {
        if (ea.in_scroll || is_window(ea) || ea.id == 0) {
            continue;
        }
        for (const auto &eb : b.entries) {
            if (!eb.in_scroll && eb.id == ea.id && (ea.x0 != eb.x0 || ea.y0 != eb.y0 || ea.x1 != eb.x1 || ea.y1 != eb.y1)) {
                return name(ea) + " " + rect(ea) + " became " + rect(eb);
            }
        }
    }
    return s13::windows_difference(without_lines(a), without_lines(b));
}

// "" or the first violation of `f`'s view at `min` over all its states: every element whole inside the tile, no item over
// another, and no item or window of the states that keep the page moving.
inline std::string run_states(fixture &f, minimum min)
{
    const ImRect tile(0, 0, min.w, min.h);
    rv_editor_probe_record first;
    bool have_first = false;
    for (size_t i = 0; i < f.states.size(); ++i) {
        f.states[i].set();
        const rv_editor_probe_record record = f.settled(min.w, min.h);
        const std::string at = "state " + std::to_string(i) + ": ";
        if (const std::string bad = rv_editor_fits_whole(record, tile); !bad.empty()) {
            return at + bad;
        }
        if (const std::string bad = overlap(record); !bad.empty()) {
            return at + bad;
        }
        if (f.states[i].mode) {
            continue;
        }
        if (!have_first) {
            first = record;
            have_first = true;
        } else if (const std::string d = moved(first, record); !d.empty()) {
            return at + d;
        }
    }
    return "";
}

inline bool contains(const std::string &s, const std::string &part)
{
    return s.find(part) != std::string::npos;
}

// An entry whose label before "##" is `text`, outside scroll areas; nullptr when none.
inline const rv_editor_probe_entry *item_named(const rv_editor_probe_record &r, const std::string &text)
{
    for (const auto &e : r.entries) {
        if (!e.in_scroll && e.label.substr(0, e.label.find("##")) == text) {
            return &e;
        }
    }
    return nullptr;
}

// An entry whose id label has `part` in it, outside scroll areas.
inline const rv_editor_probe_entry *item_with(const rv_editor_probe_record &r, const std::string &part)
{
    for (const auto &e : r.entries) {
        if (!e.in_scroll && contains(e.label, part)) {
            return &e;
        }
    }
    return nullptr;
}

inline size_t items_in_scroll(const rv_editor_probe_record &r)
{
    size_t n = 0;
    for (const auto &e : r.entries) {
        n += e.in_scroll && !is_window(e) ? 1 : 0;
    }
    return n;
}

// Moves the pointer to the centre of `e`, presses and releases over a few frames, drawing the page each time.
inline void click(fixture &f, const rv_editor_probe_entry &e, float width, float height)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    f.draw(width, height);
    io.AddMouseButtonEvent(0, true);
    f.draw(width, height);
    io.AddMouseButtonEvent(0, false);
    f.draw(width, height);
    f.draw(width, height);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    f.draw(width, height);
}

} // namespace s19

} // namespace rv_editor_test
