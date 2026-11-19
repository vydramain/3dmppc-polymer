// The content area: three rows plus the horizontal bar at the least, the code background, the empty state that keeps
// the headers, bars only on overflow, and everything whole at the tile's minimum.

#include <vector>

#include "rv_editor_test_content.hpp"
#include "rv_editor_test_fits_whole.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::content;

namespace
{

void rows(int count, int long_at = -1, size_t long_chars = 400)
{
    for (int i = 0; i < count; ++i) {
        const std::string label = i == long_at ? long_text(long_chars) : "row " + std::to_string(i);
        rv_editor_list_row(("r" + std::to_string(i)).c_str(), label.c_str(), theme);
    }
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_content_min_height_holds_three_rows_and_the_horizontal_bar)
{
    load_texts();
    rv_editor_test_imgui gui;
    float min_h = 0.0f;
    float line = 0.0f;
    float spacing_row = 0.0f;
    float bar = 0.0f;
    ImRect inner;
    ImRect first[3];
    const auto r = settled(gui, 300, 300, [&](rv_editor_probe_record &) {
        min_h = rv_editor_content_min_height();
        line = ImGui::GetTextLineHeight();
        spacing_row = ImGui::GetFrameHeightWithSpacing();
        bar = ImGui::GetStyle().ScrollbarSize;
        rv_editor_content_begin("c", ImVec2(240, min_h), theme);
        inner = ImGui::GetCurrentWindow()->InnerRect;
        for (int i = 0; i < 8; ++i) {
            const std::string label = i == 5 ? long_text(400) : "row " + std::to_string(i);
            rv_editor_list_row(("r" + std::to_string(i)).c_str(), label.c_str(), theme);
            if (i < 3) {
                first[i] = last_item();
            }
        }
        rv_editor_content_end(theme);
    });
    RV_EDITOR_CHECK(min_h >= 3.0f * line + bar);
    RV_EDITOR_CHECK(min_h <= 3.0f * spacing_row + bar + 4.0f * ImGui::GetStyle().FramePadding.y + 8.0f);
    for (const ImRect &row : first) {
        RV_EDITOR_CHECK(row.Min.y >= inner.Min.y - 0.5f && row.Max.y <= inner.Max.y + 0.5f);
    }
    RV_EDITOR_CHECK(bars_of(r).h); // the long row scrolls sideways, and its bar did not take a row's room
}

RV_EDITOR_TEST(rv_editor_test_content_sits_on_the_code_background_with_code_text)
{
    load_texts();
    rv_editor_test_imgui gui;
    bool painted = false;
    ImU32 text_inside = 0;
    ImU32 text_before = 0;
    ImU32 text_after = 0;
    ImU32 bg_after = 0;
    ImU32 bg_before = 0;
    rv_editor_probe_draw(gui, 300, [&](rv_editor_probe_record &) {
        text_before = ImGui::GetColorU32(ImGuiCol_Text);
        bg_before = ImGui::GetColorU32(ImGuiCol_ChildBg);
        rv_editor_content_begin("c", ImVec2(240, 100), theme);
        text_inside = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 base = rv_editor_col(theme.code_base);
        for (const ImGuiWindow *w = ImGui::GetCurrentWindow(); w != nullptr && !painted; w = w->ParentWindow) {
            for (int i = 0; i < w->DrawList->VtxBuffer.Size && !painted; ++i) {
                painted = w->DrawList->VtxBuffer[i].col == base;
            }
        }
        rv_editor_content_end(theme);
        text_after = ImGui::GetColorU32(ImGuiCol_Text);
        bg_after = ImGui::GetColorU32(ImGuiCol_ChildBg);
    });
    RV_EDITOR_CHECK(painted);
    RV_EDITOR_CHECK(text_inside == rv_editor_col(theme.code_text));
    RV_EDITOR_CHECK(text_after == text_before && bg_after == bg_before); // the colours are given back
}

RV_EDITOR_TEST(rv_editor_test_content_empty_state_keeps_background_and_header_rectangles)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string hint = "No problems. A build lists here what the compiler finds.";
    struct seen {
        rv_editor_probe_record header; // everything drawn up to the first row
        ImRect first_cell;             // the first row's first item
        bool painted = false;
        size_t items_before = 0, items_after = 0;
        float hint_w = 0.0f;
    };
    const auto draw_state = [&](bool empty) {
        seen s;
        const auto r = settled(gui, 300, 300, [&](rv_editor_probe_record &rec) {
            rv_editor_content_begin("c", ImVec2(260, 120), theme);
            const ImU32 base = rv_editor_col(theme.code_base);
            s.painted = false;
            for (const ImGuiWindow *w = ImGui::GetCurrentWindow(); w != nullptr && !s.painted; w = w->ParentWindow) {
                for (int i = 0; i < w->DrawList->VtxBuffer.Size && !s.painted; ++i) {
                    s.painted = w->DrawList->VtxBuffer[i].col == base;
                }
            }
            rv_editor_table_column cols[2];
            cols[0].name = "Name";
            cols[1].name = "Status";
            cols[1].width = rv_editor_table_width::fixed;
            cols[1].fixed_px = 70.0f;
            cols[1].pinned_right = true;
            rv_editor_table_style style;
            style.header = true;
            if (rv_editor_table_begin("t", cols, 2, style)) {
                s.header.entries = rec.entries;
                s.hint_w = ImGui::CalcTextSize(hint.c_str()).x;
                s.items_before = rec.entries.size();
                if (empty) {
                    rv_editor_table_row();
                    rv_editor_table_cell();
                    rv_editor_content_empty_row(hint.c_str(), theme);
                } else {
                    for (int i = 0; i < 3; ++i) {
                        rv_editor_table_row();
                        rv_editor_table_cell();
                        rv_editor_table_cell_text(("item " + std::to_string(i)).c_str(), theme);
                        if (i == 0) {
                            s.first_cell = last_item();
                        }
                        rv_editor_table_cell();
                        rv_editor_status("ok", rv_editor_status_kind::ok, theme);
                    }
                }
                if (empty) {
                    s.first_cell = last_item();
                }
                s.items_after = rec.entries.size();
                rv_editor_table_end();
            }
            rv_editor_content_end(theme);
        });
        (void)r;
        return s;
    };
    const seen data = draw_state(false);
    const seen empty = draw_state(true);
    RV_EDITOR_CHECK(data.painted && empty.painted);
    RV_EDITOR_CHECK(!empty.header.entries.empty());
    RV_EDITOR_CHECK(rv_editor_probe_compare(data.header, empty.header).empty());
    RV_EDITOR_CHECK(empty.items_after > empty.items_before);
    // The hint is the first row: where the first item of the first row is with data, whole and on one line.
    RV_EDITOR_CHECK(near(empty.first_cell.Min.y, data.first_cell.Min.y));
    RV_EDITOR_CHECK(near(empty.first_cell.Min.x, data.first_cell.Min.x));
    RV_EDITOR_CHECK(empty.first_cell.GetWidth() >= empty.hint_w - 0.5f);
    RV_EDITOR_CHECK(empty.first_cell.GetHeight() < ImGui::GetFontSize() * 2.0f);
}

RV_EDITOR_TEST(rv_editor_test_content_bars_show_only_on_overflow)
{
    load_texts();
    rv_editor_test_imgui gui;
    const auto draw = [&](int count, int long_at) {
        return bars_of(settled(gui, 300, 300, [&](rv_editor_probe_record &) {
            rv_editor_content_begin("c", ImVec2(240, 100), theme);
            rows(count, long_at);
            rv_editor_content_end(theme);
        }));
    };
    const bars few = draw(2, -1);
    RV_EDITOR_CHECK(!few.v && !few.h);
    const bars tall = draw(40, -1);
    RV_EDITOR_CHECK(tall.v && !tall.h);
    const bars wide = draw(2, 1);
    RV_EDITOR_CHECK(!wide.v && wide.h);
    const bars both = draw(40, 3);
    RV_EDITOR_CHECK(both.v && both.h);
}

RV_EDITOR_TEST(rv_editor_test_content_fits_whole_at_the_minimum_size)
{
    load_texts();
    rv_editor_test_imgui gui;
    const auto body = [&](rv_editor_probe_record &) {
        ImGui::TextUnformatted("Build #12 failed");
        rv_editor_content_begin("c", ImVec2(0, rv_editor_content_min_height()), theme);
        rows(8, 4);
        rv_editor_content_end(theme);
    };
    // The window is the tile: its height is what the body takes at the minimum (the title line, the content, the
    // padding), measured first in a tall window.
    const float needed = settled(gui, 260, 300, body).content_height + ImGui::GetStyle().WindowPadding.y;
    const auto r = settled(gui, 260, needed, body);
    const ImRect tile(0, 0, 260, needed);
    RV_EDITOR_CHECK(rv_editor_fits_whole(r, tile).empty());
    RV_EDITOR_CHECK(area_of(r) != nullptr);
    RV_EDITOR_CHECK(bars_of(r).h);
    RV_EDITOR_CHECK(area_of(r) != nullptr && area_of(r)->y1 - area_of(r)->y0 >= 3.0f * ImGui::GetFontSize());
}

RV_EDITOR_TEST(rv_editor_test_content_keeps_its_place_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    const auto draw = [&](int state) {
        return settled(gui, 300, 300, [&](rv_editor_probe_record &) {
            ImGui::TextUnformatted("Summary line");
            rv_editor_content_begin("c", ImVec2(0, rv_editor_content_min_height() + 40.0f), theme);
            if (state == 0) {
                rv_editor_content_empty_row("Nothing here yet", theme);
            } else {
                rows(state == 1 ? 2 : 40, state == 3 ? 1 : -1);
            }
            rv_editor_content_end(theme);
        });
    };
    const auto first = draw(0);
    RV_EDITOR_CHECK(area_of(first) != nullptr);
    for (int state = 1; state < 4; ++state) {
        const auto other = draw(state);
        RV_EDITOR_CHECK(area_of(first) != nullptr && area_of(other) != nullptr);
        if (area_of(first) != nullptr && area_of(other) != nullptr) {
            const auto *a = area_of(first);
            const auto *b = area_of(other);
            RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1);
        }
        RV_EDITOR_CHECK(!first.entries.empty() && !other.entries.empty() && first.entries[0].y0 == other.entries[0].y0 &&
            first.entries[0].y1 == other.entries[0].y1);
    }
}
