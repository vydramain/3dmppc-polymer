// Helpers of the empty row tests: the probe reading of a table's empty row (its text, and the button that stands in its
// row) and the probe window they draw in. The log tests in rv_editor_test_empty_row.cpp and the Search and Assets tests
// in rv_editor_test_empty_row_panes.cpp read the row through them.

#pragma once

#include <cstdio>
#include <string>

#include "rv_editor_test_s13.hpp"

namespace rv_editor_test
{

namespace empty_rows
{

inline constexpr float width = 900.0f;
inline constexpr float height = 640.0f;

using entry = rv_editor_probe_entry;
using record = rv_editor_probe_record;

// The empty row is the item whose id is "empty" (rv_editor_content_empty_row), its text is the label before the marker.
inline const entry *empty_row(const record &r)
{
    for (const auto &e : r.entries) {
        if (e.label.find("###empty") != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

inline std::string row_text(const entry &row)
{
    return row.label.substr(0, row.label.find("###empty"));
}

inline bool has(const std::string &text, const char *word)
{
    return text.find(word) != std::string::npos;
}

// A named item inside a content area, other than the empty row, whose middle lies within the row's height: the button of
// the row.
inline const entry *row_button(const record &r, const entry &row)
{
    for (const auto &e : r.entries) {
        const float middle = (e.y0 + e.y1) / 2.0f;
        if (&e != &row && e.in_scroll && !e.scroll_area && e.id != 0 && !e.label.empty() && e.x1 - e.x0 > 1.0f &&
            middle >= row.y0 - 1.0f && middle <= row.y1 + 1.0f) {
            return &e;
        }
    }
    return nullptr;
}

inline void report(const char *what, const std::string &detail)
{
    std::fprintf(stderr, "  %s: %s\n", what, detail.c_str());
}

} // namespace empty_rows

} // namespace rv_editor_test
