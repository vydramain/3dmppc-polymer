// The Export tile redesign: its blocks, the kind cards, the Export button's reason, the result line.

#include <string>
#include <string_view>

#include "imgui_internal.h"
#include "panes/rv_editor_pane_export.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;

namespace
{

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

rv_editor_progress progress_with(rv_editor_step_state a, rv_editor_step_state b, rv_editor_run_end end)
{
    rv_editor_progress p;
    rv_editor_run run;
    run.tool = "burner";
    run.steps.resize(3);
    run.steps[0].label = "sources";
    run.steps[0].state = rv_editor_step_state::done;
    run.steps[1].label = "compile";
    run.steps[1].state = a;
    run.steps[2].label = "assets";
    run.steps[2].state = b;
    run.end = end;
    p.runs.push_back(run);
    return p;
}

// How many windows have `part` in the last segment of their name (the part after the last '/'),
// so a child nested in another child is counted only once, under its own name.
int children_named(const char *part)
{
    int n = 0;
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        const std::string_view name = w->Name;
        const std::string_view last = name.substr(name.find_last_of('/') + 1);
        n += last.find(part) != std::string_view::npos ? 1 : 0;
    }
    return n;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_export_tile_blocks)
{
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        const rv_editor_export_blocks b = rv_editor_export_blocks_shown(info.kind);
        RV_EDITOR_CHECK(b.kind && b.output && b.progress);
        RV_EDITOR_CHECK(b.web_tools == info.needs_web_tools);
    }
}

RV_EDITOR_TEST(rv_editor_test_export_tile_card_columns)
{
    RV_EDITOR_CHECK(rv_editor_export_card_columns(1000.0f, 200.0f, 10.0f, 3) == 3);
    RV_EDITOR_CHECK(rv_editor_export_card_columns(1000.0f, 200.0f, 10.0f, 2) == 2);
    RV_EDITOR_CHECK(rv_editor_export_card_columns(430.0f, 200.0f, 10.0f, 3) == 2);
    RV_EDITOR_CHECK(rv_editor_export_card_columns(100.0f, 200.0f, 10.0f, 3) == 1);
}

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

// The pane draws one card child per row of the kinds table (named "##export_card_<subdir>") and one child per block.
RV_EDITOR_TEST(rv_editor_test_export_tile_pane_draws_cards_and_blocks)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    app.project.open = true;
    const rv_editor_theme theme = rv_editor_theme_olive;
    int cards = 0;
    int per_kind[3] = {};
    int blocks[4] = {};
    gui.frame([&]() {
        rv_editor_pane_export(app, theme);
        cards = children_named("##export_card_");
        for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
            const std::string name = "##export_card_" + std::string(info.subdir);
            per_kind[static_cast<size_t>(info.kind)] = children_named(name.c_str());
        }
        blocks[0] = children_named("##export_block_kind");
        blocks[1] = children_named("##export_block_web_tools");
        blocks[2] = children_named("##export_block_output");
        blocks[3] = children_named("##export_block_progress");
    });
    RV_EDITOR_CHECK(cards == static_cast<int>(rv_editor_export_kinds().size()));
    for (const int n : per_kind) {
        RV_EDITOR_CHECK(n == 1);
    }
    RV_EDITOR_CHECK(blocks[0] == 1);
    RV_EDITOR_CHECK(blocks[1] == 0); // the first kind (disc) needs no web tools
    RV_EDITOR_CHECK(blocks[2] == 1);
    RV_EDITOR_CHECK(blocks[3] == 1);
}
