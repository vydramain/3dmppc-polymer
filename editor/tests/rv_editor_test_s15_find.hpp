// Finders of the S15 tests: the kinds, and message box, content areas and items by their visible text in a probe record.

#pragma once

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s15
{

using namespace rv_editor;
namespace fs = std::filesystem;

using s13::content_area;
using s13::has_ask_place;
using s13::is_area_scroll;
using s13::is_child;
using s13::load_texts;
using s13::message_box;
using s13::parts_of;
using s13::same_rect;
using s13::windows_difference;
using s13::words;

inline constexpr rv_editor_pane_id pane = 1;
inline constexpr int files_count = 30; // enough rows for the Files tree to scroll in a 400 px tile

inline const std::vector<rv_editor_pane_kind> &kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::project,
        rv_editor_pane_kind::files,
        rv_editor_pane_kind::assets,
        rv_editor_pane_kind::run_config,
        rv_editor_pane_kind::findings,
        rv_editor_pane_kind::test_case };
    return all;
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::project:
        return "project";
    case rv_editor_pane_kind::files:
        return "files";
    case rv_editor_pane_kind::assets:
        return "assets";
    case rv_editor_pane_kind::run_config:
        return "run_config";
    case rv_editor_pane_kind::findings:
        return "findings";
    default:
        return "test_case";
    }
}

inline uint64_t revision = 9000; // Run Config refills its form when this moves

// The visible text of an item's label (the part before "##").
inline std::string text_of(const rv_editor_probe_entry &e)
{
    return e.label.substr(0, e.label.find("##"));
}

inline const rv_editor_probe_entry *find_text(const rv_editor_probe_record &r, const std::string &text)
{
    for (const auto &e : r.entries) {
        if (!text.empty() && text_of(e) == text) {
            return &e;
        }
    }
    return nullptr;
}

// An item whose label starts with `id` ("##field").
inline const rv_editor_probe_entry *find_id(const rv_editor_probe_record &r, const std::string &id)
{
    for (const auto &e : r.entries) {
        if (e.label.compare(0, id.size(), id) == 0 && e.label.find('/') == std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

inline bool ends_with(const std::string &s, const char *suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Every content area frame of the record.
inline std::vector<const rv_editor_probe_entry *> areas(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (is_child(parts_of(e.label).back(), "##area")) {
            out.push_back(&e);
        }
    }
    return out;
}

inline bool inside(const rv_editor_probe_entry &e, const rv_editor_probe_entry &area)
{
    const float cx = (e.x0 + e.x1) / 2.0f;
    const float cy = (e.y0 + e.y1) / 2.0f;
    return cx >= area.x0 && cx <= area.x1 && cy >= area.y0 && cy <= area.y1;
}

// Items drawn inside `area` that are not windows: its rows, cells and fields.
inline int items_in(const rv_editor_probe_record &r, const rv_editor_probe_entry &area)
{
    int n = 0;
    for (const auto &e : r.entries) {
        n += e.in_scroll && !e.scroll_area && e.x1 - e.x0 > 1.0f && inside(e, area) ? 1 : 0;
    }
    return n;
}

// Items inside `area` whose label ends with `suffix`.
inline int labelled_in(const rv_editor_probe_record &r, const rv_editor_probe_entry &area, const char *suffix)
{
    int n = 0;
    for (const auto &e : r.entries) {
        n += !e.scroll_area && ends_with(e.label, suffix) && inside(e, area) ? 1 : 0;
    }
    return n;
}

} // namespace s15

} // namespace rv_editor_test
