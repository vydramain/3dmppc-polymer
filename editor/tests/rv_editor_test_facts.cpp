// The facts list: whole values under horizontal scroll, keys pinned left, bars only on overflow, "-" for a missing
// value, Copy in the row menu, the code background and everything whole at the tile's minimum.

#include <vector>

#include "rv_editor_test_content.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "ui/rv_editor_widgets_facts.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::content;

namespace
{

constexpr float area_w = 240.0f;
const std::string sha = "b4bdd5f80ffbec286eb3836dcdea18be2e042c6fc9a4e5de94e870fac388bfed";
const std::string path = long_text(100);

struct fact {
    const char *key;
    const char *value; // null = missing
};

// Candidate, SHA-256 and Image as the Release Candidate shows them.
std::vector<fact> candidate()
{
    return { { "Candidate", "#3" }, { "SHA-256", sha.c_str() }, { "Size", "10365976 bytes" }, { "Image", path.c_str() } };
}

struct seen {
    ImRect inner;
    float font = 0.0f;
    bool painted = false;
    std::vector<rv_editor_facts_row_result> rows;
    rv_editor_probe_record record;
};

seen draw_facts(rv_editor_test_imgui &gui, const std::vector<fact> &facts, float w, float h, bool scroll_right = false)
{
    seen s;
    s.record = settled(gui, w + 40.0f, h + 40.0f, [&](rv_editor_probe_record &) {
        s.font = ImGui::GetFontSize();
        rv_editor_facts_begin("f", ImVec2(w, h), theme);
        if (scroll_right) {
            ImGui::SetScrollX(100000.0f);
        }
        s.inner = ImGui::GetCurrentWindow()->InnerRect;
        const ImU32 base = rv_editor_col(theme.code_base);
        s.painted = false;
        for (const ImGuiWindow *win = ImGui::GetCurrentWindow(); win != nullptr && !s.painted; win = win->ParentWindow) {
            for (int i = 0; i < win->DrawList->VtxBuffer.Size && !s.painted; ++i) {
                s.painted = win->DrawList->VtxBuffer[i].col == base;
            }
        }
        s.rows.clear();
        for (const fact &f : facts) {
            s.rows.push_back(rv_editor_facts_row(f.key, f.value, theme));
        }
        rv_editor_facts_end(theme);
    });
    return s;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == label) {
            return &e;
        }
    }
    return nullptr;
}

float width_of(const rv_editor_facts_row_result &r)
{
    return r.value_max.x - r.value_min.x;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_facts_show_long_values_whole_under_horizontal_scroll)
{
    load_texts();
    rv_editor_test_imgui gui;
    const seen s = draw_facts(gui, candidate(), area_w, 120);
    RV_EDITOR_CHECK(s.rows.size() == 4);
    for (const size_t i : { size_t(1), size_t(3) }) { // SHA-256 and Image
        const std::string value = i == 1 ? sha : path;
        RV_EDITOR_CHECK(width_of(s.rows[i]) >= ImGui::CalcTextSize(value.c_str()).x - 0.5f);
        RV_EDITOR_CHECK(s.rows[i].value_max.x > s.inner.Max.x); // longer than the area, so it scrolls
    }
    RV_EDITOR_CHECK(!shows_ellipsis(s.record));
    RV_EDITOR_CHECK(bars_of(s.record).h);
}

RV_EDITOR_TEST(rv_editor_test_facts_keys_stay_at_the_left_edge_after_horizontal_scroll)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const bool right : { false, true }) {
        const seen s = draw_facts(gui, candidate(), area_w, 120, right);
        RV_EDITOR_CHECK(s.rows.size() == 4);
        for (const auto &row : s.rows) {
            RV_EDITOR_CHECK(row.key_max.x > row.key_min.x);
            RV_EDITOR_CHECK(row.key_min.x >= s.inner.Min.x - 0.5f && row.key_min.x - s.inner.Min.x <= s.font);
        }
        if (right && s.rows.size() == 4) {
            RV_EDITOR_CHECK(s.rows[1].value_min.x < s.inner.Min.x); // the values did scroll away
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_facts_bars_show_only_on_overflow)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::vector<fact> few = { { "Size", "10 bytes" }, { "Name", "disc" } };
    std::vector<fact> many;
    for (int i = 0; i < 12; ++i) {
        many.push_back({ "Size", "10 bytes" });
    }
    const bars none = bars_of(draw_facts(gui, few, 300, 120).record);
    RV_EDITOR_CHECK(!none.h && !none.v);
    const bars wide = bars_of(draw_facts(gui, candidate(), area_w, 160).record);
    RV_EDITOR_CHECK(wide.h && !wide.v);
    const bars tall = bars_of(draw_facts(gui, many, 300, 100).record);
    RV_EDITOR_CHECK(tall.v && !tall.h);
    const bars both = bars_of(draw_facts(
        gui,
        [&] {
            auto f = many;
            f.push_back({ "Image", path.c_str() });
            return f;
        }(),
        area_w,
        100)
            .record);
    RV_EDITOR_CHECK(both.h && both.v);
}

RV_EDITOR_TEST(rv_editor_test_facts_missing_value_shows_a_dash_and_keeps_the_row)
{
    load_texts();
    rv_editor_test_imgui gui;
    const seen s = draw_facts(gui, { { "Revision", nullptr }, { "Playtest", "" }, { "Size", "1 byte" } }, 300, 120);
    RV_EDITOR_CHECK(s.rows.size() == 3);
    if (s.rows.size() == 3) {
        const float dash = ImGui::CalcTextSize("-").x;
        RV_EDITOR_CHECK(near(width_of(s.rows[0]), dash) && near(width_of(s.rows[1]), dash));
        RV_EDITOR_CHECK(near(width_of(s.rows[2]), ImGui::CalcTextSize("1 byte").x));
        RV_EDITOR_CHECK(s.rows[1].key_min.y > s.rows[0].key_min.y && s.rows[2].key_min.y > s.rows[1].key_min.y);
    }
}

RV_EDITOR_TEST(rv_editor_test_facts_row_menu_copies_the_rows_value)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string copy = rv_editor_text("widgets_fields.button_copy");
    const auto facts = candidate();
    seen last;
    const auto draw = [&] {
        last = draw_facts(gui, facts, 300, 160);
        return last.record;
    };
    for (const size_t row : { size_t(1), size_t(3) }) { // SHA-256 and Image
        const std::string value = row == 1 ? sha : path;
        ImGui::SetClipboardText("");
        draw();
        const rv_editor_probe_record before = draw();
        RV_EDITOR_CHECK(find(before, copy) == nullptr); // the menu is closed until a right click
        RV_EDITOR_CHECK(last.rows.size() == 4);
        if (last.rows.size() != 4) {
            continue;
        }
        const auto &cell = last.rows[row];
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(cell.value_min.x + 4.0f, (cell.value_min.y + cell.value_max.y) / 2.0f);
        draw();
        io.AddMouseButtonEvent(1, true);
        draw();
        io.AddMouseButtonEvent(1, false);
        draw();
        const rv_editor_probe_record open = draw();
        const auto *item = find(open, copy);
        RV_EDITOR_CHECK(item != nullptr);
        if (item != nullptr) {
            io.AddMousePosEvent((item->x0 + item->x1) / 2.0f, (item->y0 + item->y1) / 2.0f);
            draw();
            io.AddMouseButtonEvent(0, true);
            draw();
            io.AddMouseButtonEvent(0, false);
            draw();
        }
        const char *clip = ImGui::GetClipboardText();
        RV_EDITOR_CHECK(clip != nullptr && value == clip);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        draw();
    }
}

RV_EDITOR_TEST(rv_editor_test_facts_row_menu_opens_on_the_key_and_beside_a_short_value)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string copy = rv_editor_text("widgets_fields.button_copy");
    const auto facts = candidate();
    seen last;
    const auto draw = [&] {
        last = draw_facts(gui, facts, 300, 160);
        return last.record;
    };
    // Right-clicks at (x, y), picks Copy if the row menu opened, and returns the clipboard text.
    const auto right_click_copy = [&](float x, float y) {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetClipboardText("");
        draw();
        io.AddMousePosEvent(x, y);
        draw();
        io.AddMouseButtonEvent(1, true);
        draw();
        io.AddMouseButtonEvent(1, false);
        draw();
        const rv_editor_probe_record open = draw();
        const auto *item = find(open, copy);
        RV_EDITOR_CHECK(item != nullptr);
        if (item != nullptr) {
            io.AddMousePosEvent((item->x0 + item->x1) / 2.0f, (item->y0 + item->y1) / 2.0f);
            draw();
            io.AddMouseButtonEvent(0, true);
            draw();
            io.AddMouseButtonEvent(0, false);
            draw();
        }
        const char *clip = ImGui::GetClipboardText();
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        draw();
        return std::string(clip != nullptr ? clip : "");
    };
    draw();
    const auto rows = last.rows;
    RV_EDITOR_CHECK(rows.size() == 4);
    if (rows.size() != 4) {
        return;
    }
    const auto mid_y = [](const rv_editor_facts_row_result &r) {
        return (r.key_min.y + r.key_max.y) / 2.0f;
    };
    // Right click on the key of a row copies that row's value.
    RV_EDITOR_CHECK(right_click_copy(rows[1].key_min.x + 4.0f, mid_y(rows[1])) == sha);
    RV_EDITOR_CHECK(right_click_copy(rows[3].key_min.x + 4.0f, mid_y(rows[3])) == path);
    // Right click in the row's free space beside a short value copies that row's value.
    const float beside = (rows[0].value_max.x + last.inner.Max.x) / 2.0f;
    RV_EDITOR_CHECK(beside > rows[0].value_max.x);
    RV_EDITOR_CHECK(right_click_copy(beside, mid_y(rows[0])) == "#3");
}

RV_EDITOR_TEST(rv_editor_test_facts_sit_on_the_code_background)
{
    load_texts();
    rv_editor_test_imgui gui;
    RV_EDITOR_CHECK(draw_facts(gui, candidate(), area_w, 120).painted);
}

RV_EDITOR_TEST(rv_editor_test_facts_fit_whole_at_the_tile_minimum)
{
    load_texts();
    rv_editor_test_imgui gui;
    float min_h = 0.0f;
    float pad = 0.0f;
    int rows = 0;
    const std::vector<fact> facts = candidate();
    const float w = area_w;
    const auto draw = [&](float h) {
        return rv_editor_probe_draw(
            gui,
            w,
            [&](rv_editor_probe_record &) {
                min_h = rv_editor_content_min_height();
                pad = ImGui::GetStyle().WindowPadding.x;
                rv_editor_facts_begin("f", ImVec2(w - 2.0f * pad, min_h), theme);
                rows = 0;
                for (const fact &f : facts) {
                    const auto row = rv_editor_facts_row(f.key, f.value, theme);
                    rows += row.key_max.x > row.key_min.x ? 1 : 0;
                }
                rv_editor_facts_end(theme);
            },
            h);
    };
    draw(300);
    const float h = min_h + 2.0f * ImGui::GetStyle().WindowPadding.y;
    draw(h);
    const rv_editor_probe_record r = draw(h);
    RV_EDITOR_CHECK(rows == 4); // every fact is drawn
    RV_EDITOR_CHECK(rv_editor_fits_whole(r, ImRect(0, 0, w, h)).empty());
    RV_EDITOR_CHECK(!shows_ellipsis(r));
}
