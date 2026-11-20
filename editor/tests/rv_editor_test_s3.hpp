// Helpers of the message strip tests of the remaining panes (S3): the old message box is a "##text" scroll child; the
// strip is one item under every content area. Finders and the comparison "a message appears and nothing above it moves".

#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_strip.hpp"

namespace rv_editor_test
{

namespace s3
{

// True when `text` is the text child of a message strip: the strip registers one frame item whose corner lies one frame
// padding up and left of the text.
inline bool in_strip_frame(const rv_editor_probe_record &r, const rv_editor_probe_entry &text)
{
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    for (const auto &o : r.entries) {
        if (!o.scroll_area && o.id != 0 && std::fabs(o.x0 - (text.x0 - pad.x)) < 0.5f &&
            std::fabs(o.y0 - (text.y0 - pad.y)) < 0.5f && o.x1 >= text.x1 - 0.5f && o.y1 >= text.y1 - 0.5f) {
            return true;
        }
    }
    return false;
}

// The text windows of the old message box (rv_editor_message_box) in the record, top to bottom. The strip's text is a
// "##text" scroll child too, but under the area's frame (s13::is_under_area) and inside the strip's frame, without the
// window padding; the old box is a window of its own exactly two lines and the padding high.
inline std::vector<const rv_editor_probe_entry *> old_boxes(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (e.scroll_area && s13::is_child(s13::parts_of(e.label).back(), "##text") &&
            std::fabs((e.y1 - e.y0) - (2.0f * ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y)) < 0.5f &&
            !in_strip_frame(r, e) && !s13::is_under_area(r, e)) {
            out.push_back(&e);
        }
    }
    std::sort(out.begin(), out.end(), [](auto *a, auto *b) {
        return a->y0 < b->y0;
    });
    return out;
}

inline int boxes(const rv_editor_probe_record &r)
{
    return static_cast<int>(old_boxes(r).size());
}

inline std::vector<const rv_editor_probe_entry *> areas(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (s13::is_child(s13::parts_of(e.label).back(), "##area")) {
            out.push_back(&e);
        }
    }
    std::sort(out.begin(), out.end(), [](auto *a, auto *b) {
        return a->y0 < b->y0;
    });
    return out;
}

// The strip: the widest item outside scroll areas that starts at or under the bottom of every content area; nullptr when
// there is none.
inline const rv_editor_probe_entry *strip_of(const rv_editor_probe_record &r)
{
    const auto all = areas(r);
    if (all.empty()) {
        return nullptr;
    }
    float bottom = 0.0f;
    for (const auto *a : all) {
        bottom = std::max(bottom, a->y1);
    }
    const rv_editor_probe_entry *best = nullptr;
    for (const auto &e : r.entries) {
        if (!e.in_scroll && !e.scroll_area && e.y0 >= bottom - 0.5f && e.y1 > e.y0 &&
            (best == nullptr || e.x1 - e.x0 > best->x1 - best->x0)) {
            best = &e;
        }
    }
    return best;
}

// The gap between `area`'s top and what is drawn above it that is not a message box: the bottom of the lowest labelled item
// ending at or above the area's top (the shelf), or the window's top when there is none.
inline float gap_above(const rv_editor_probe_record &r, const rv_editor_probe_entry &area)
{
    float above = 0.0f;
    bool any = false;
    for (const auto &e : r.entries) {
        const auto last = s13::parts_of(e.label).back();
        if (e.label.empty() || s13::is_child(last, "##text") || e.y1 > area.y0 + 0.5f || e.y1 <= e.y0) {
            continue;
        }
        above = std::max(above, e.y1);
        any = true;
    }
    return area.y0 - (any ? above : 0.0f);
}

inline std::string rect_of(const rv_editor_probe_entry &e)
{
    return rv_editor_probe_detail::rect(e);
}

// "" when every item of `without` that ends at or above its first content area's top is in `with` with the same rectangle.
inline std::string above_area_same(const rv_editor_probe_record &without, const rv_editor_probe_record &with)
{
    const auto aw = areas(without);
    if (aw.empty()) {
        return "no content area";
    }
    for (const auto &e : without.entries) {
        if (e.in_scroll || e.y1 > aw[0]->y0 + 0.5f) {
            continue;
        }
        const rv_editor_probe_entry *twin = nullptr;
        for (const auto &o : with.entries) {
            twin = o.id == e.id ? &o : twin;
        }
        if (twin == nullptr) {
            return "'" + e.label + "' " + rect_of(e) + " is gone";
        }
        if (twin->x0 != e.x0 || twin->y0 != e.y0 || twin->x1 != e.x1 || twin->y1 != e.y1) {
            return "'" + e.label + "' " + rect_of(e) + " became " + rect_of(*twin);
        }
    }
    return "";
}

// "" when `with` has the message as a strip under its areas inside the tile of `height`, nothing above the first area moved,
// and the last area ended higher by at least the strip's height (it loses height only at its bottom).
inline std::string strip_appears(const rv_editor_probe_record &without, const rv_editor_probe_record &with, float height)
{
    if (boxes(with) > boxes(without)) {
        return "the message is a box";
    }
    if (const std::string moved = above_area_same(without, with); !moved.empty()) {
        return moved;
    }
    const rv_editor_probe_entry *strip = strip_of(with);
    if (strip == nullptr) {
        return "no strip under the areas";
    }
    if (strip->y1 > height + 0.5f) {
        return "the strip " + rect_of(*strip) + " ends under the tile";
    }
    const auto before = areas(without);
    const auto after = areas(with);
    if (before.size() != after.size()) {
        return "the number of areas changed";
    }
    const float lost = before.back()->y1 - after.back()->y1;
    if (lost < strip->y1 - strip->y0 - 0.5f) {
        return "the last area lost " + std::to_string(lost) + " px, the strip is " + std::to_string(strip->y1 - strip->y0);
    }
    // The gap is measured from the area's frame (the item the area registers), not from its window, which is a pad smaller.
    if (strip->y0 - strip::frame_of(with, after.back())->y1 > ImGui::GetStyle().ItemSpacing.y + 1.0f) {
        return "the strip is not right under the last area";
    }
    return "";
}

// Like s13::windows_difference (the scroll windows and content area frames of two draws of one layout are in the same
// place), except that a window may end higher or lower: a strip under the areas takes height from their bottom edge. The
// strip and its scrolling text lie under the area's frame and are not compared: the shelf, the top and the width of the
// area are.
inline std::string windows_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto one_way = [](const rv_editor_probe_record &x, const rv_editor_probe_record &y, const char *where) {
        for (const auto &ex : x.entries) {
            const bool frame = s13::is_child(s13::parts_of(ex.label).back(), "##area");
            if ((!ex.scroll_area && !frame) || s13::is_area_scroll(ex) || s13::is_under_area(x, ex)) {
                continue;
            }
            const rv_editor_probe_entry *found = nullptr;
            for (const auto &ey : y.entries) {
                found = found == nullptr && (ey.scroll_area || frame) && ey.label == ex.label ? &ey : found;
            }
            if (found == nullptr) {
                return ex.label + " is missing from the " + where + " draw";
            }
            if (found->x0 != ex.x0 || found->y0 != ex.y0 || found->x1 != ex.x1) {
                return ex.label + " " + rect_of(ex) + " became " + rect_of(*found) + " (the bottom edge may move)";
            }
        }
        return std::string();
    };
    const std::string d = one_way(a, b, "second");
    return d.empty() ? one_way(b, a, "first") : d;
}

} // namespace s3

} // namespace rv_editor_test
