// Helpers of the message strip tests of the panes: no window of the old message box, and what a strip under a content area
// may and may not change in the draw of a pane (a probe record of the state without the message against the one with it).

#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "rv_editor_test_s13.hpp"

namespace rv_editor_test
{

namespace strip
{

using entry = rv_editor_probe_entry;
using record = rv_editor_probe_record;

// True when a window of the old message box (rv_editor_message_box and its ask places) is anywhere in the record.
inline bool has_message_window(const record &r)
{
    for (const auto &e : r.entries) {
        for (const auto &part : s13::parts_of(e.label)) {
            for (const char *id : { "##notes", "##note", "##say", "##ask" }) {
                if (part == id || s13::is_child(part, id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

inline const entry *by_id(const record &r, ImGuiID id)
{
    for (const auto &e : r.entries) {
        if (id != 0 && e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

// The first and the last content area frame of the record.
inline const entry *first_area(const record &r)
{
    return s13::content_area(r);
}

inline const entry *last_area(const record &r)
{
    const entry *found = nullptr;
    for (const auto &e : r.entries) {
        found = s13::is_child(s13::parts_of(e.label).back(), "##area") ? &e : found;
    }
    return found;
}

inline int area_count(const record &r)
{
    int n = 0;
    for (const auto &e : r.entries) {
        n += s13::is_child(s13::parts_of(e.label).back(), "##area") ? 1 : 0;
    }
    return n;
}

// The frame of an area: its window is a little smaller (the theme's pad), the item rv_editor_tiled_area_end registers is
// not, and the strip's gap and the bottom the area gives up are measured from that. The smallest entry that holds the
// window and reaches lower than it, the window itself when nothing does.
inline const entry *frame_of(const record &r, const entry *area)
{
    const entry *best = area;
    float best_size = 1.0e12f;
    for (const auto &e : r.entries) {
        const float size = (e.x1 - e.x0) * (e.y1 - e.y0);
        if (area != nullptr && &e != area && e.id != 0 && !e.in_scroll && e.x0 <= area->x0 + 0.5f && e.y0 <= area->y0 + 0.5f &&
            e.x1 >= area->x1 - 0.5f && e.y1 >= area->y1 - 0.5f && e.y1 > area->y1 + 0.5f && size < best_size) {
            best = &e;
            best_size = size;
        }
    }
    return best;
}

// The first item drawn inside the first area that is not a window: its first row.
inline const entry *first_row(const record &r)
{
    const entry *area = first_area(r);
    for (const auto &e : r.entries) {
        if (area != nullptr && e.in_scroll && !e.scroll_area && e.id != 0 && e.x1 - e.x0 > 1.0f && e.y0 >= area->y0 &&
            e.y1 <= area->y1) {
            return &e;
        }
    }
    return nullptr;
}

inline bool same_box(const entry &a, const entry &b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

// "" when `with` has the item of `base` (by id) at the very same rectangle, else what is wrong.
inline std::string stays(const entry &base, const record &with, const char *what)
{
    const entry *other = by_id(with, base.id);
    if (other == nullptr) {
        return std::string(what) + " is missing with the message";
    }
    return same_box(base, *other) ? "" : std::string(what) + " " + rv_editor_probe_detail::rect(base) + " became " +
            rv_editor_probe_detail::rect(*other);
}

// What is new in `said` under the area frame `area`: not in the record without the message (null: all of it).
inline std::vector<const entry *> fresh_below(const record *quiet, const record &said, const entry *area)
{
    std::vector<const entry *> fresh;
    for (const auto &e : said.entries) {
        if (!e.in_scroll && e.id != 0 && e.y0 >= area->y1 - 0.5f && (quiet == nullptr || by_id(*quiet, e.id) == nullptr)) {
            fresh.push_back(&e);
        }
    }
    return fresh;
}

inline const entry *widest(const std::vector<const entry *> &items)
{
    // The frame spans the pane's width; the text of a long message scrolls inside it and may be taller than the frame.
    const entry *w = items.front();
    for (const entry *e : items) {
        w = e->x1 - e->x0 > w->x1 - w->x0 + 0.5f ? e : w;
    }
    return w;
}

// The height of the strip `said` has under its last area, 0 when it has none.
inline float strip_height(const record *quiet, const record &said)
{
    const entry *window = last_area(said);
    if (window == nullptr) {
        return 0.0f;
    }
    const auto fresh = fresh_below(quiet, said, frame_of(said, window));
    return fresh.empty() ? 0.0f : widest(fresh)->y1 - widest(fresh)->y0;
}

// "" when both records have the first content area at the same top and width; the strip may move its bottom.
inline std::string same_top(const record &a, const record &b)
{
    const entry *x = first_area(a);
    const entry *y = first_area(b);
    if (x == nullptr || y == nullptr || (x->x0 == y->x0 && x->y0 == y->y0 && x->x1 == y->x1)) {
        return "";
    }
    return "the first content area moved: " + rv_editor_probe_detail::rect(*x) + " became " + rv_editor_probe_detail::rect(*y);
}

// "" when `said` has a strip under its last content area and, given the draw without the message, nothing above it moved;
// else the first thing wrong. The strip is what is new under the area: one frame item with the buttons inside it. `quiet`
// null: nothing to compare with, the strip alone is checked. `rows` false: the first row is not compared (a log or a screen
// whose lines the state itself changes). `tile` is the size the pane was drawn in.
inline std::string
strip_problems(const record *quiet, const record &said, ImVec2 tile, float gap, float frame_h, bool rows = true)
{
    const entry *window1 = last_area(said);
    if (window1 == nullptr) {
        return "no content area";
    }
    const entry *area1 = frame_of(said, window1);
    const std::vector<const entry *> fresh = fresh_below(quiet, said, area1);
    if (fresh.empty()) {
        return "no strip under the content area";
    }
    const entry *frame = widest(fresh);
    for (const entry *e : fresh) {
        // Only the named items (buttons, the field): the text of a long message is a rectangle that scrolls in the strip.
        if (!e->label.empty() && (e->x0 < frame->x0 - 0.5f || e->x1 > frame->x1 + 0.5f || e->y0 < frame->y0 - 0.5f || e->y1 > frame->y1 + 0.5f)) {
            return "an item of the strip (a button or the name field) lies outside its frame";
        }
    }
    const float strip_h = frame->y1 - frame->y0;
    if (strip_h < frame_h - 0.5f) {
        return "the strip is lower than one frame";
    }
    if (frame->y0 < area1->y1 - 0.5f || frame->y0 - area1->y1 > gap + 0.5f) {
        return "the strip is not right under the area (" + std::to_string(frame->y0 - area1->y1) + " px between)";
    }
    if (frame->x0 < -0.5f || frame->x1 > tile.x + 0.5f || frame->y1 > tile.y + 0.5f) {
        return "the strip is outside the tile";
    }
    if (quiet == nullptr) {
        return "";
    }
    const entry *window0 = last_area(*quiet);
    const entry *area0 = frame_of(*quiet, window0);
    const entry *top0 = first_area(*quiet);
    const entry *top1 = first_area(said);
    if (window0 == nullptr || top0 == nullptr || top1 == nullptr) {
        return "no content area without the message";
    }
    if (top0->x0 != top1->x0 || top0->y0 != top1->y0 || top0->x1 != top1->x1) {
        return "the first area moved or changed its width with the message";
    }
    if (area_count(*quiet) == 1 && window0->y0 != window1->y0) {
        return "the area's top moved with the message";
    }
    const float lost = area0->y1 - area1->y1;
    const float want = strip_h + (frame->y0 - area1->y1);
    if (std::fabs(lost - want) > 0.5f) {
        return "the area's bottom rose by " + std::to_string(lost) + " px, the strip and its gap are " + std::to_string(want);
    }
    for (const auto &e : quiet->entries) {
        if (e.in_scroll || e.id == 0 || &e == area0 || &e == window0 || &e == top0) {
            continue;
        }
        const bool above = e.y1 <= top0->y0 + 0.5f;
        const bool below = e.y0 >= area0->y1 - 0.5f;
        if (above || below) {
            if (const std::string moved = stays(e, said, above ? "an item above the area" : "an item under the area");
                !moved.empty()) {
                return moved;
            }
        }
    }
    if (const entry *row = rows ? first_row(*quiet) : nullptr; row != nullptr) {
        // Its right edge may give way to the scroll bar the shorter area now needs; its top and bottom may not move.
        const entry *other = by_id(said, row->id);
        if (other == nullptr || other->x0 != row->x0 || other->y0 != row->y0 || other->y1 != row->y1) {
            return "the first row moved with the message";
        }
    }
    return "";
}

// The record without what lies in the first area's vertical band and what is inside a scroll: the shelf, the header and what
// stands under the area stay, the area (its bottom is the strip's) goes.
inline record outside_area(const record &r)
{
    record out;
    out.content_height = r.content_height;
    const entry *area = first_area(r);
    for (const auto &e : r.entries) {
        const bool in_band = area != nullptr && e.y0 >= area->y0 - 0.5f && e.y0 < area->y1 - 0.5f;
        if (!e.in_scroll && !in_band) {
            out.entries.push_back(e);
        }
    }
    return out;
}

// The record as R2 promises it stays: what is above the last area and the area's top and width. The strip under the area
// takes the area's bottom, so the bottom edge of the areas (window, frame, scroll window, `extra`: the labels of other
// items that give up their bottom, the Scene's viewport), the height of the content and everything under the area's frame
// (the strip and its text) are left out; `inside` false leaves out what is inside a scroll too. Two such records are compared
// with any "same layout" check.
inline record bottom_free(const record &r, const std::vector<std::string> &extra = {}, bool inside = true)
{
    record out;
    const entry *last = last_area(r);
    const entry *frame = last != nullptr ? frame_of(r, last) : nullptr;
    for (const auto &e : r.entries) {
        if ((frame != nullptr && !e.in_scroll && &e != frame && e.y0 >= frame->y1 - 0.5f) || (!inside && e.in_scroll)) {
            continue;
        }
        out.entries.push_back(e);
        entry &x = out.entries.back();
        bool free = e.label.find("##area") != std::string::npos || e.scroll_area;
        for (const std::string &label : extra) {
            free = free || e.label == label;
        }
        for (const auto &a : r.entries) {
            // The frame is the item rv_editor_tiled_area_end registers: as high as the area's window or a little higher.
            const bool area = s13::is_child(s13::parts_of(a.label).back(), "##area");
            free = free ||
                (area &&
                    (&e == frame_of(r, &a) ||
                        (e.label.empty() && e.id != 0 && e.x0 <= a.x0 + 0.5f && e.y0 <= a.y0 + 0.5f && e.y0 >= a.y0 - 12.0f &&
                            e.x1 >= a.x1 - 0.5f && e.y1 >= a.y1 - 0.5f)));
        }
        if (free) {
            x.y1 = x.y0;
            x.x1 = x.x0 + (s13::is_area_scroll(e) ? 0.0f : e.x1 - e.x0); // a bar narrows the scroll window
        }
    }
    return out;
}

} // namespace strip

} // namespace rv_editor_test
