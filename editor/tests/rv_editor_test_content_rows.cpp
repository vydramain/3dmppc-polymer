// Rows in a content area: one width for every row, the selection across all of it, columns pinned to an edge, whole
// text, and record rows of exactly two lines with the status at the right.

#include <vector>

#include "rv_editor_test_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::content;

namespace
{

constexpr float area_w = 240.0f;

struct list_seen {
    std::vector<ImRect> rows;
    ImRect inner;
    float long_w = 0.0f;
    float font = 0.0f;
    rv_editor_probe_record record;
};

// Rows labelled `labels` (the one at `selected` selected) in a content area; the area is scrolled right when asked.
list_seen draw_list(rv_editor_test_imgui &gui, const std::vector<std::string> &labels, int selected, bool right)
{
    list_seen s;
    s.record = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        s.rows.clear();
        s.long_w = 0.0f;
        s.font = ImGui::GetFontSize();
        rv_editor_content_begin("c", ImVec2(area_w, 120), theme);
        if (right) {
            ImGui::SetScrollX(100000.0f);
        }
        s.inner = ImGui::GetCurrentWindow()->InnerRect;
        for (size_t i = 0; i < labels.size(); ++i) {
            s.long_w = std::max(s.long_w, ImGui::CalcTextSize(labels[i].c_str()).x);
            rv_editor_list_style style;
            style.selected = static_cast<int>(i) == selected;
            rv_editor_list_row(("r" + std::to_string(i)).c_str(), labels[i].c_str(), theme, style);
            s.rows.push_back(last_item());
        }
        rv_editor_content_end(theme);
    });
    return s;
}

bool equal_widths(const std::vector<ImRect> &rows)
{
    for (const ImRect &r : rows) {
        if (!near(r.GetWidth(), rows[0].GetWidth())) {
            return false;
        }
    }
    return !rows.empty();
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_content_list_rows_are_as_wide_as_the_area_when_all_are_short)
{
    load_texts();
    rv_editor_test_imgui gui;
    const list_seen s = draw_list(gui, { "a", "bb", "a longer one" }, -1, false);
    RV_EDITOR_CHECK(equal_widths(s.rows));
    RV_EDITOR_CHECK(area_of(s.record) != nullptr);
    RV_EDITOR_CHECK(s.rows[0].Min.x <= s.inner.Min.x + s.font && s.rows[0].Max.x >= s.inner.Max.x - s.font);
    RV_EDITOR_CHECK(!bars_of(s.record).h);
}

RV_EDITOR_TEST(rv_editor_test_content_list_rows_are_as_wide_as_the_longest_row)
{
    load_texts();
    rv_editor_test_imgui gui;
    const list_seen s = draw_list(gui, { "short", long_text(300), "middle sized row" }, -1, false);
    RV_EDITOR_CHECK(equal_widths(s.rows));
    RV_EDITOR_CHECK(s.rows[0].GetWidth() >= s.long_w);
    RV_EDITOR_CHECK(s.rows[0].GetWidth() <= s.long_w + 4.0f * s.font);
    RV_EDITOR_CHECK(bars_of(s.record).h);
    RV_EDITOR_CHECK(!shows_ellipsis(s.record));
}

RV_EDITOR_TEST(rv_editor_test_content_selection_spans_the_whole_row_width_at_any_scroll)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const bool right : { false, true }) {
        const list_seen s = draw_list(gui, { "short", long_text(300), "chosen" }, 2, right);
        // The chosen row is as wide as the longest and reaches both edges of the visible area it spans.
        RV_EDITOR_CHECK(s.rows[2].GetWidth() >= s.long_w);
        RV_EDITOR_CHECK(s.rows[2].Min.x <= s.inner.Min.x + 0.5f + (right ? 0.0f : s.font));
        RV_EDITOR_CHECK(s.rows[2].Max.x >= s.inner.Max.x - 0.5f - (right ? s.font : 0.0f));
        RV_EDITOR_CHECK(right ? s.rows[2].Min.x < s.inner.Min.x : s.rows[2].Max.x > s.inner.Max.x);
    }
}

RV_EDITOR_TEST(rv_editor_test_content_tree_rows_share_one_right_edge)
{
    load_texts();
    rv_editor_test_imgui gui;
    std::vector<ImRect> rows;
    float long_w = 0.0f;
    const std::string long_name = long_text(300);
    const auto r = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        rows.clear();
        long_w = ImGui::CalcTextSize(long_name.c_str()).x;
        rv_editor_content_begin("c", ImVec2(area_w, 120), theme);
        rv_editor_tree_style root;
        root.default_open = true;
        const auto top = rv_editor_tree_row("src", "src", theme, root);
        rows.push_back(last_item());
        if (top.open) {
            rv_editor_tree_style leaf;
            leaf.leaf = true;
            leaf.no_push = true;
            leaf.selected = true;
            rv_editor_tree_row("a", "a.cpp", theme, leaf);
            rows.push_back(last_item());
            rv_editor_tree_row("b", long_name.c_str(), theme, leaf);
            rows.push_back(last_item());
            rv_editor_tree_pop();
        }
        rv_editor_content_end(theme);
    });
    RV_EDITOR_CHECK(rows.size() == 3);
    for (const ImRect &row : rows) {
        RV_EDITOR_CHECK(near(row.Max.x, rows[0].Max.x));
    }
    // The deepest row holds its whole name: its right edge is past where the name ends.
    RV_EDITOR_CHECK(rows.size() == 3 && rows[2].Max.x >= rows[2].Min.x + long_w);
    RV_EDITOR_CHECK(bars_of(r).h);
    RV_EDITOR_CHECK(!shows_ellipsis(r));
}

namespace
{

struct pinned_seen {
    ImRect inner;
    ImRect first_name;
    ImRect status;
    ImRect row;
    float font = 0.0f;
    float message_w = 0.0f;
    rv_editor_probe_record record;
};

// A table of name, message and status; `pin_*` pins the first column left and the last right.
pinned_seen draw_table(rv_editor_test_imgui &gui, size_t message_chars, bool right, bool pin_left, bool pin_right)
{
    pinned_seen s;
    const std::string message = long_text(message_chars);
    s.record = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        s.font = ImGui::GetFontSize();
        s.message_w = ImGui::CalcTextSize(message.c_str()).x;
        rv_editor_content_begin("c", ImVec2(area_w, 120), theme);
        if (right) {
            ImGui::SetScrollX(100000.0f);
        }
        s.inner = ImGui::GetCurrentWindow()->InnerRect;
        rv_editor_table_column cols[3];
        cols[0].name = "Name";
        cols[0].pinned_left = pin_left;
        cols[1].name = "Message";
        cols[2].name = "Status";
        cols[2].pinned_right = pin_right;
        rv_editor_table_style style;
        style.header = true;
        style.sizing = rv_editor_table_sizing::fixed_fit;
        if (rv_editor_table_begin("t", cols, 3, style)) {
            for (int i = 0; i < 3; ++i) {
                rv_editor_table_row();
                rv_editor_table_cell();
                const auto row = rv_editor_table_row_select(("##row" + std::to_string(i)).c_str(), i == 1, theme);
                (void)row;
                if (i == 1) {
                    s.row = last_item();
                }
                rv_editor_table_cell_text("unit_name", theme);
                if (i == 0) {
                    s.first_name = last_item();
                }
                rv_editor_table_cell();
                rv_editor_table_cell_text(i == 0 ? message.c_str() : "short", theme);
                rv_editor_table_cell();
                rv_editor_status("Available", rv_editor_status_kind::ok, theme);
                if (i == 0) {
                    s.status = last_item();
                }
            }
            rv_editor_table_end();
        }
        rv_editor_content_end(theme);
    });
    return s;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_content_pinned_right_column_stays_at_the_right_edge_while_scrolling)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const size_t chars : { size_t(20), size_t(300) }) {
        for (const bool right : { false, true }) {
            const pinned_seen s = draw_table(gui, chars, right, false, true);
            RV_EDITOR_CHECK(s.status.Max.x <= s.inner.Max.x + 0.5f);
            RV_EDITOR_CHECK(s.inner.Max.x - s.status.Max.x <= s.font);
            RV_EDITOR_CHECK(s.status.GetWidth() > 0.0f && s.status.GetHeight() < s.font * 2.0f);
            RV_EDITOR_CHECK(chars < 100 || bars_of(s.record).h);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_content_pinned_left_column_stays_at_the_left_edge_while_scrolling)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const bool right : { false, true }) {
        const pinned_seen s = draw_table(gui, 300, right, true, false);
        RV_EDITOR_CHECK(s.first_name.Min.x >= s.inner.Min.x - 0.5f);
        RV_EDITOR_CHECK(s.first_name.Min.x - s.inner.Min.x <= s.font);
        RV_EDITOR_CHECK(bars_of(s.record).h);
    }
}

RV_EDITOR_TEST(rv_editor_test_content_table_selection_covers_the_whole_table_width)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const bool right : { false, true }) {
        const pinned_seen s = draw_table(gui, 300, right, false, true);
        RV_EDITOR_CHECK(s.row.GetWidth() >= s.message_w);
        RV_EDITOR_CHECK(s.row.Min.x <= s.inner.Min.x + (right ? 0.5f : s.font));
        RV_EDITOR_CHECK(s.row.Max.x >= s.inner.Max.x - (right ? s.font : 0.5f));
    }
}

RV_EDITOR_TEST(rv_editor_test_content_table_cells_keep_their_whole_text)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string cell = long_text(300);
    float text_w = 0.0f;
    ImRect rect;
    const auto r = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        text_w = ImGui::CalcTextSize(cell.c_str()).x;
        rv_editor_content_begin("c", ImVec2(120, 100), theme);
        rv_editor_table_column cols[1];
        cols[0].name = "Cell";
        rv_editor_table_style style;
        style.header = true;
        style.sizing = rv_editor_table_sizing::fixed_fit;
        if (rv_editor_table_begin("t", cols, 1, style)) {
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_table_cell_text(cell.c_str(), theme);
            rect = last_item();
            rv_editor_table_end();
        }
        rv_editor_content_end(theme);
    });
    RV_EDITOR_CHECK(near(rect.GetWidth(), text_w, 1.0f));
    RV_EDITOR_CHECK(rect.GetHeight() < ImGui::GetFontSize() * 2.0f);
    RV_EDITOR_CHECK(bars_of(r).h);
    RV_EDITOR_CHECK(!shows_ellipsis(r));
}

namespace
{

struct record_seen {
    std::vector<rv_editor_record_result> rows;
    ImRect inner;
    float font = 0.0f;
    float line = 0.0f;
    float frame_spaced = 0.0f;
    float widest_word = 0.0f;
    float status_w = 0.0f;
    float path_w = 0.0f;
    rv_editor_probe_record record;
};

record_seen draw_records(rv_editor_test_imgui &gui, float width, bool right, bool selected_second)
{
    record_seen s;
    const std::string long_name = long_text(120);
    const std::string long_path = "/home/user/" + long_text(200);
    const char *const words[] = { "Available", "Missing" };
    s.record = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        s.rows.clear();
        s.font = ImGui::GetFontSize();
        s.line = ImGui::GetTextLineHeight();
        s.frame_spaced = ImGui::GetFrameHeightWithSpacing();
        s.widest_word = ImGui::CalcTextSize("Available").x;
        s.path_w = ImGui::CalcTextSize(long_path.c_str()).x;
        s.status_w = rv_editor_record_status_width(words, 2);
        rv_editor_content_begin("c", ImVec2(width, 140), theme);
        if (right) {
            ImGui::SetScrollX(100000.0f);
        }
        s.inner = ImGui::GetCurrentWindow()->InnerRect;
        rv_editor_record_style style;
        style.status_width = s.status_w;
        s.rows.push_back(
            rv_editor_record_row("a", "solid", "/home/user/repo", "Available", rv_editor_status_kind::ok, theme, style));
        style.selected = selected_second;
        s.rows.push_back(rv_editor_record_row("b",
            long_name.c_str(),
            long_path.c_str(),
            "Missing",
            rv_editor_status_kind::error,
            theme,
            style));
        style.selected = false;
        s.rows.push_back(rv_editor_record_row("c", "mid", "/tmp/mid", "Available", rv_editor_status_kind::ok, theme, style));
        rv_editor_content_end(theme);
    });
    return s;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_content_record_rows_are_two_lines_whatever_the_length)
{
    load_texts();
    rv_editor_test_imgui gui;
    const record_seen s = draw_records(gui, area_w, false, false);
    RV_EDITOR_CHECK(s.rows.size() == 3);
    for (const auto &row : s.rows) {
        const float h = row.row_max.y - row.row_min.y;
        RV_EDITOR_CHECK(near(h, s.rows[0].row_max.y - s.rows[0].row_min.y));
        RV_EDITOR_CHECK(h >= 2.0f * s.line - 0.5f && h < 3.0f * s.frame_spaced);
        RV_EDITOR_CHECK(near(row.row_max.x - row.row_min.x, s.rows[0].row_max.x - s.rows[0].row_min.x));
    }
    // The whole path is in the row, and the area scrolls sideways for it.
    RV_EDITOR_CHECK(s.rows.size() == 3 && s.rows[1].row_max.x - s.rows[1].row_min.x >= s.path_w);
    RV_EDITOR_CHECK(bars_of(s.record).h);
    RV_EDITOR_CHECK(!shows_ellipsis(s.record));
    // A narrower area changes no row's height: the status does not wrap by letters.
    const record_seen narrow = draw_records(gui, 110.0f, false, false);
    RV_EDITOR_CHECK(narrow.rows.size() == 3 && s.rows.size() == 3);
    if (narrow.rows.size() == 3 && s.rows.size() == 3) {
        RV_EDITOR_CHECK(near(narrow.rows[0].row_max.y - narrow.rows[0].row_min.y, s.rows[0].row_max.y - s.rows[0].row_min.y));
    }
}

RV_EDITOR_TEST(rv_editor_test_content_record_status_is_whole_and_pinned_at_the_right_edge)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const float width : { area_w, 110.0f }) {
        for (const bool right : { false, true }) {
            const record_seen s = draw_records(gui, width, right, false);
            RV_EDITOR_CHECK(s.status_w >= s.widest_word);
            RV_EDITOR_CHECK(s.rows.size() == 3);
            for (const auto &row : s.rows) {
                RV_EDITOR_CHECK(near(row.status_max.x - row.status_min.x, s.status_w));
                RV_EDITOR_CHECK(row.status_max.x <= s.inner.Max.x + 0.5f);
                RV_EDITOR_CHECK(s.inner.Max.x - row.status_max.x <= s.font);
                RV_EDITOR_CHECK(row.status_max.y - row.status_min.y <= row.row_max.y - row.row_min.y + 0.5f);
            }
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_content_record_selection_covers_the_whole_row)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const bool right : { false, true }) {
        const record_seen s = draw_records(gui, area_w, right, true);
        RV_EDITOR_CHECK(s.rows.size() == 3);
        if (s.rows.size() == 3) {
            const auto &row = s.rows[1];
            RV_EDITOR_CHECK(row.row_max.x - row.row_min.x >= s.path_w);
            RV_EDITOR_CHECK(row.row_min.x <= s.inner.Min.x + (right ? 0.5f : s.font));
            RV_EDITOR_CHECK(row.row_max.x >= s.inner.Max.x - (right ? s.font : 0.5f));
        }
    }
}
