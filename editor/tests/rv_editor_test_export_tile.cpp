// The Export tile v2, first part: the kind dropdown over the kinds table, the result and button rules, and the
// constant description area.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "imgui_internal.h"
#include "panes/rv_editor_pane_export.hpp"
#include "platform/rv_editor_process.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "rv_editor_test_export_tile.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::export_tile;

RV_EDITOR_TEST(rv_editor_test_export_tile_kind_change_while_busy)
{
    RV_EDITOR_CHECK(rv_editor_export_kind_change_allowed(false));
    RV_EDITOR_CHECK(!rv_editor_export_kind_change_allowed(true));
}

RV_EDITOR_TEST(rv_editor_test_export_tile_button_why)
{
    load_texts();
    // Live: any kind with its tools, no job.
    RV_EDITOR_CHECK(rv_editor_export_button_why(rv_editor_export_kind::disc, false, false) == nullptr);
    RV_EDITOR_CHECK(rv_editor_export_button_why(rv_editor_export_kind::player, false, false) == nullptr);
    RV_EDITOR_CHECK(rv_editor_export_button_why(rv_editor_export_kind::web, true, false) == nullptr);
    // Web without its tools: grey with a reason; the other kinds do not care.
    const char *web = rv_editor_export_button_why(rv_editor_export_kind::web, false, false);
    RV_EDITOR_CHECK(web != nullptr && *web != '\0');
    // A job runs: grey with a reason for every kind.
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        const char *busy = rv_editor_export_button_why(info.kind, true, true);
        RV_EDITOR_CHECK(busy != nullptr && *busy != '\0');
    }
    const char *busy_web = rv_editor_export_button_why(rv_editor_export_kind::web, false, true);
    RV_EDITOR_CHECK(web != nullptr && busy_web != nullptr && std::string_view(web) != busy_web);
}

RV_EDITOR_TEST(rv_editor_test_export_tile_result_none)
{
    const auto v =
        rv_editor_export_result_make(rv_editor_build_state::idle, "", "", rv_editor_progress{}, rv_editor_export_kind::disc);
    RV_EDITOR_CHECK(v.kind == rv_editor_export_result_kind::none);
    RV_EDITOR_CHECK(!v.open_folder && !v.open_browser && !v.show_problems);
}

RV_EDITOR_TEST(rv_editor_test_export_tile_result_running)
{
    const auto p = progress_with(rv_editor_step_state::running, rv_editor_step_state::pending, rv_editor_run_end::open);
    for (const auto s : { rv_editor_build_state::building, rv_editor_build_state::cancelling }) {
        const auto v = rv_editor_export_result_make(s, "", "", p, rv_editor_export_kind::player);
        RV_EDITOR_CHECK(v.kind == rv_editor_export_result_kind::running);
        RV_EDITOR_CHECK(!v.open_folder && !v.open_browser && !v.show_problems);
    }
}

RV_EDITOR_TEST(rv_editor_test_export_tile_result_succeeded_actions_from_table)
{
    const auto p = progress_with(rv_editor_step_state::done, rv_editor_step_state::done, rv_editor_run_end::ok);
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        const auto v = rv_editor_export_result_make(rv_editor_build_state::succeeded, "/p/out", "", p, info.kind);
        RV_EDITOR_CHECK(v.kind == rv_editor_export_result_kind::succeeded);
        RV_EDITOR_CHECK(v.path == "/p/out");
        RV_EDITOR_CHECK(v.open_browser == (info.open == rv_editor_export_open_action::browser));
        RV_EDITOR_CHECK(v.open_folder == (info.open == rv_editor_export_open_action::folder));
        RV_EDITOR_CHECK(!v.show_problems);
    }
}

RV_EDITOR_TEST(rv_editor_test_export_tile_result_failed)
{
    const auto p = progress_with(rv_editor_step_state::failed, rv_editor_step_state::pending, rv_editor_run_end::failed);
    const auto v =
        rv_editor_export_result_make(rv_editor_build_state::failed, "", "no space left", p, rv_editor_export_kind::disc);
    RV_EDITOR_CHECK(v.kind == rv_editor_export_result_kind::failed);
    RV_EDITOR_CHECK(v.step == "compile");
    RV_EDITOR_CHECK(v.reason == "no space left");
    RV_EDITOR_CHECK(v.show_problems);
    RV_EDITOR_CHECK(!v.open_folder && !v.open_browser);
    // A run cut short names the step it was interrupted at.
    const auto q = progress_with(rv_editor_step_state::done, rv_editor_step_state::interrupted, rv_editor_run_end::interrupted);
    RV_EDITOR_CHECK(
        rv_editor_export_result_make(rv_editor_build_state::failed, "", "killed", q, rv_editor_export_kind::web).step ==
        "assets");
    // No step failed (the burner refused to start): the step is empty, the reason stays.
    const auto none = rv_editor_export_result_make(rv_editor_build_state::failed,
        "",
        "no burner",
        rv_editor_progress{},
        rv_editor_export_kind::disc);
    RV_EDITOR_CHECK(none.kind == rv_editor_export_result_kind::failed && none.step.empty() && none.reason == "no burner");
}

RV_EDITOR_TEST(rv_editor_test_export_tile_show_in_problems)
{
    rv_editor_app app;
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::empty);
    rv_editor_export_show_problems(app);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::problems);
}

RV_EDITOR_TEST(rv_editor_test_export_tile_kind_items_follow_the_table)
{
    load_texts();
    const std::vector<std::string> items = rv_editor_export_kind_items();
    RV_EDITOR_CHECK(items.size() == rv_editor_export_kinds().size());
    for (size_t i = 0; i < items.size() && i < rv_editor_export_kinds().size(); ++i) {
        RV_EDITOR_CHECK(items[i] == rv_editor_text(rv_editor_export_kinds()[i].name_key));
    }
}

// The kind is a dropdown, not a card per kind.
RV_EDITOR_TEST(rv_editor_test_export_tile_kind_is_a_combo)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("combo");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    const rv_editor_probe_record r = draw_tile(gui, app, wide);
    RV_EDITOR_CHECK(find(r, "##export_kind_combo") != nullptr);
    RV_EDITOR_CHECK(find(r, "##export_card_") == nullptr);
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_tile_description_height_is_the_tallest)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const float width : { wide, narrow }) {
        gui.frame(
            [&] {
                float tallest = 0.0f;
                for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
                    const float h = rv_editor_export_description_wrapped_height(info.kind, width);
                    RV_EDITOR_CHECK(h > 0.0f);
                    tallest = h > tallest ? h : tallest;
                }
                RV_EDITOR_CHECK(tallest > 0.0f);
                RV_EDITOR_CHECK(rv_editor_export_description_height(width) == tallest);
            },
            width);
    }
}

// The description area is the same rectangle for every kind, as tall as the tallest description.
RV_EDITOR_TEST(rv_editor_test_export_tile_description_area_constant_across_kinds)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("desc");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    for (const float width : { wide, narrow }) {
        float tallest = 0.0f;
        gui.frame(
            [&] {
                tallest = rv_editor_export_description_height(width - 16.0f);
            },
            width);
        rv_editor_probe_record first;
        for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
            rv_editor_pane_export_select_kind(info.kind);
            const rv_editor_probe_record r = draw_tile(gui, app, width);
            const rv_editor_probe_entry *area = find(r, "##export_kind_desc");
            RV_EDITOR_CHECK(area != nullptr);
            if (area == nullptr) {
                continue;
            }
            RV_EDITOR_CHECK(area->y1 - area->y0 >= tallest);
            if (info.kind == rv_editor_export_kind::disc) {
                first = r;
            }
            RV_EDITOR_CHECK(rv_editor_probe_compare(first, r) == "");
        }
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}
