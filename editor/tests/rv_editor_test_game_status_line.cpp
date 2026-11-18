// Game status layout: one line when it fits, size and state on separate lines otherwise; the rows lie over the top
// of the picture on a translucent backing as tall as they are, and the picture takes the whole area.

#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

#include <format>
#include <string>
#include <vector>

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_game_status_rows_one_line_when_it_fits)
{
    const rv_editor_game_status_lines lines{ "320x240  Fit 2.00x", "Stopped." };
    const std::string joined = "320x240  Fit 2.00x  Stopped.";
    for (const float width : { 200.0f, 201.0f, 1000.0f }) {
        const auto rows = rv_editor_game_status_rows(lines, width, 200.0f);
        RV_EDITOR_CHECK(rows.size() == 1);
        RV_EDITOR_CHECK(!rows.empty() && rows[0] == joined);
    }
}

RV_EDITOR_TEST(rv_editor_test_game_status_rows_split_when_too_narrow)
{
    const rv_editor_game_status_lines lines{ "320x240  Fit 2.00x", "Stopped." };
    const auto rows = rv_editor_game_status_rows(lines, 199.0f, 200.0f);
    RV_EDITOR_CHECK(rows.size() == 2);
    RV_EDITOR_CHECK(rows.size() == 2 && rows[0] == "320x240  Fit 2.00x");
    RV_EDITOR_CHECK(rows.size() == 2 && rows[1] == "Stopped.");
}

RV_EDITOR_TEST(rv_editor_test_game_status_stale_session_text)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const int session = 3;
    const char *state = "Exited";
    RV_EDITOR_CHECK(
        rv_editor_text_format("pane_game.stale_session", std::make_format_args(session, state)) == "stale: session 3, Exited");
}

namespace
{

const std::vector<std::string> narrow_rows{ "320x240  Fit 2.00x", "Stopped. stale: session 3, Exited with a long state" };

bool same_view(const rv_editor_game_view &a, const rv_editor_game_view &b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.scale == b.scale && a.reduced == b.reduced;
}

} // namespace

// AC13: the picture is placed from the full area height, so the text does not change its scale.
RV_EDITOR_TEST(rv_editor_test_game_picture_view_uses_full_height)
{
    rv_editor_test::rv_editor_test_imgui gui;
    gui.frame([&] {
        const auto view = rv_editor_game_picture_view(320, 240, 640.0f, 480.0f, rv_editor_game_scale::fit);
        RV_EDITOR_CHECK(view.scale == 2.0f);
        RV_EDITOR_CHECK(view.w == 640.0f && view.h == 480.0f);
        for (const auto mode : { rv_editor_game_scale::fit, rv_editor_game_scale::integer, rv_editor_game_scale::x1 }) {
            for (const ImVec2 area : { ImVec2(640, 480), ImVec2(300, 200), ImVec2(900, 400), ImVec2(1000, 900) }) {
                RV_EDITOR_CHECK(same_view(rv_editor_game_picture_view(320, 240, area.x, area.y, mode),
                    rv_editor_game_place(320, 240, area.x, area.y, mode)));
            }
        }
    });
}

// AC15: rows follow each other without overlap and the backing is exactly as tall as all of them.
RV_EDITOR_TEST(rv_editor_test_game_status_overlay_backing_is_as_tall_as_rows)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    gui.frame([&] {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 origin(10.0f, 20.0f);
        for (const float width : { 70.0f, 120.0f, 400.0f }) {
            const std::vector<std::vector<std::string>> cases{ { "Stopped." }, narrow_rows };
            for (const auto &rows : cases) {
                const auto o = rv_editor_game_status_overlay(dl, rows, rv_editor_label_kind::normal, theme, origin, width);
                RV_EDITOR_CHECK(o.rows.size() == rows.size());
                if (o.rows.size() != rows.size()) {
                    continue;
                }
                float total = 0.0f;
                for (size_t i = 0; i < rows.size(); i++) {
                    const float h = ImGui::CalcTextSize(rows[i].c_str(), nullptr, false, width).y;
                    RV_EDITOR_CHECK(o.rows[i].max.y - o.rows[i].min.y == h);
                    total += h;
                    if (i > 0) {
                        RV_EDITOR_CHECK(o.rows[i - 1].max.y <= o.rows[i].min.y);
                    }
                }
                RV_EDITOR_CHECK(o.backing.min.x == origin.x && o.backing.min.y == origin.y);
                RV_EDITOR_CHECK(o.backing.max.x == origin.x + width);
                RV_EDITOR_CHECK(o.backing.max.y - o.backing.min.y == total);
                RV_EDITOR_CHECK(o.rows.front().min.y >= o.backing.min.y && o.rows.back().max.y == o.backing.max.y);
            }
        }
        // A narrow width wraps the long row over several lines, and the backing grows with it.
        const auto o = rv_editor_game_status_overlay(dl, narrow_rows, rv_editor_label_kind::normal, theme, origin, 70.0f);
        RV_EDITOR_CHECK(o.backing.max.y - o.backing.min.y > 3.0f * ImGui::GetTextLineHeight());
    });
}

// AC14, AC15: the picture goes first, then a translucent theme-colour backing, then the text in the kind's colour.
RV_EDITOR_TEST(rv_editor_test_game_status_overlay_draws_after_picture)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    struct kind_case {
        rv_editor_label_kind kind;
        uint32_t colour;
    };
    const kind_case kinds[] = { { rv_editor_label_kind::normal, theme.text },
        { rv_editor_label_kind::warning, theme.warning },
        { rv_editor_label_kind::dim, theme.text_disabled } };
    for (const auto &k : kinds) {
        gui.frame([&] {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const ImVec2 origin(10.0f, 20.0f);
            const int base = dl->VtxBuffer.Size;
            dl->AddImage(ImTextureID(1), origin, ImVec2(300, 250));
            const int image_end = dl->VtxBuffer.Size;
            RV_EDITOR_CHECK(image_end == base + 4);
            const auto o =
                rv_editor_game_status_overlay(dl, { "320x240  Fit 2.00x", "Stopped." }, k.kind, theme, origin, 280.0f);
            // 4 vertices of the backing rectangle follow the image, then the glyphs.
            RV_EDITOR_CHECK(dl->VtxBuffer.Size > image_end + 4);
            if (dl->VtxBuffer.Size <= image_end + 4) {
                return;
            }
            for (int i = image_end; i < image_end + 4; i++) {
                const ImDrawVert &v = dl->VtxBuffer[i];
                const ImU32 alpha = v.col >> IM_COL32_A_SHIFT;
                RV_EDITOR_CHECK(alpha > 0 && alpha < 255);
                RV_EDITOR_CHECK(v.pos.x == o.backing.min.x || v.pos.x == o.backing.max.x);
                RV_EDITOR_CHECK(v.pos.y == o.backing.min.y || v.pos.y == o.backing.max.y);
            }
            for (int i = image_end + 4; i < dl->VtxBuffer.Size; i++) {
                RV_EDITOR_CHECK(dl->VtxBuffer[i].col == rv_editor_col(k.colour));
            }
        });
    }
}

// AC16: a click anywhere over the picture, where the text lies too, hits the click area.
RV_EDITOR_TEST(rv_editor_test_game_click_rect_covers_text)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    gui.frame([&] {
        const ImVec2 origin(10.0f, 20.0f), area(300.0f, 200.0f);
        const auto click = rv_editor_game_click_rect(origin, area);
        RV_EDITOR_CHECK(click.min.x == origin.x && click.min.y == origin.y);
        RV_EDITOR_CHECK(click.max.x == origin.x + area.x && click.max.y == origin.y + area.y);
        const auto o = rv_editor_game_status_overlay(ImGui::GetWindowDrawList(),
            narrow_rows,
            rv_editor_label_kind::normal,
            theme,
            origin,
            area.x);
        RV_EDITOR_CHECK(!o.rows.empty());
        RV_EDITOR_CHECK(o.backing.min.x >= click.min.x && o.backing.min.y >= click.min.y);
        RV_EDITOR_CHECK(o.backing.max.x <= click.max.x && o.backing.max.y <= click.max.y);
        for (const auto &r : o.rows) {
            RV_EDITOR_CHECK(r.min.y >= click.min.y && r.max.y <= click.max.y);
        }
    });
}
