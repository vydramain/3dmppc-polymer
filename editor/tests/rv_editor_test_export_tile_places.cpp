// The Export tile v2, second part: the tile keeps every block in place by kind and by state; Web Tools, result buttons
// and the Directory field.

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

// The whole tile is the same for Disc, Bundle and Web; the kind dropdown and the result buttons are always there.
RV_EDITOR_TEST(rv_editor_test_export_tile_same_places_for_every_kind)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("kinds");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    for (const float width : { wide, narrow }) {
        rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
        const rv_editor_probe_record disc = draw_tile(gui, app, width);
        RV_EDITOR_CHECK(find(disc, "##export_kind_combo") != nullptr);
        for (const char *b : result_buttons) {
            RV_EDITOR_CHECK(find(disc, b) != nullptr);
        }
        for (const rv_editor_export_kind kind : { rv_editor_export_kind::player, rv_editor_export_kind::web }) {
            rv_editor_pane_export_select_kind(kind);
            const rv_editor_probe_record other = draw_tile(gui, app, width);
            RV_EDITOR_CHECK(rv_editor_probe_compare(disc, other) == "");
        }
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

// A kind without web tools has a disabled Web Tools block that says so; a kind with tools has no such note.
RV_EDITOR_TEST(rv_editor_test_export_tile_web_tools_disabled_text)
{
    load_texts();
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        const char *note = rv_editor_export_web_tools_disabled_text(info.kind);
        if (info.needs_web_tools) {
            RV_EDITOR_CHECK(note == nullptr);
        } else {
            RV_EDITOR_CHECK(note != nullptr && *note != '\0');
        }
    }
}

// The tile keeps every block in place while an export is none, running, succeeded, failed or cancelled.
RV_EDITOR_TEST(rv_editor_test_export_tile_same_places_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("states");
    for (const float width : { wide, narrow }) {
        rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
        rv_editor_probe_record none;
        {
            rv_editor_app app;
            make_app(app, base / "none", script(base, "exit 0"));
            none = draw_tile(gui, app, width);
        }
        RV_EDITOR_CHECK(find(none, "##export_kind_combo") != nullptr);
        struct run_case {
            const char *name;
            const char *body;
            rv_editor_build_state want;
            bool cancel;
        };
        const run_case cases[] = {
            { "running",
                "echo 'mppcburner: [plan] sources compile assets'; echo 'mppcburner: [1/3] sources x'; sleep 30",
                rv_editor_build_state::building,
                false },
            { "succeeded", "mkdir -p \"$6\" && echo x > \"$6/new.txt\"", rv_editor_build_state::succeeded, false },
            { "failed",
                "echo 'mppcburner: [plan] sources compile'; echo boom >&2; exit 3",
                rv_editor_build_state::failed,
                false },
            { "cancelled", "sleep 30", rv_editor_build_state::cancelled, true },
        };
        for (const run_case &c : cases) {
            const fs::path dir = base / c.name;
            std::error_code ec;
            fs::create_directories(dir, ec);
            rv_editor_app app;
            make_app(app, dir, script(dir, c.body));
            rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
            RV_EDITOR_CHECK(rv_editor_pane_export_start(app) == RV_OK);
            if (c.cancel) {
                std::string error;
                RV_EDITOR_CHECK(app.exporter.cancel(error) == RV_OK);
            }
            settle(app, c.want);
            RV_EDITOR_CHECK(app.exporter.state() == c.want);
            const rv_editor_probe_record r = draw_tile(gui, app, width);
            const std::string diff = rv_editor_probe_compare(none, r);
            RV_EDITOR_CHECK(diff == "");
            if (!diff.empty()) {
                std::fprintf(stderr, "state %s width %.0f: %s\n", c.name, width, diff.c_str());
            }
            std::string error;
            app.exporter.cancel(error);
            settle(app, rv_editor_build_state::cancelled);
        }
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

// The Web Tools block keeps its place while a web setup runs, and for the kinds that need none.
RV_EDITOR_TEST(rv_editor_test_export_tile_web_tools_block_same_rect_in_setup)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("setup");
    rv_editor_app app;
    make_app(app,
        base,
        script(base, "echo 'mppcburner: [plan] download unpack'; echo 'mppcburner: [1/2] download x 10%'; sleep 30"));
    rv_editor_pane_export_select_kind(rv_editor_export_kind::web);
    const rv_editor_probe_record idle = draw_tile(gui, app, wide);
    std::string setup_error;
    RV_EDITOR_CHECK(app.exporter.start_setup(app.tools, app.log, setup_error) == RV_OK);
    for (int i = 0; i < 25 && app.exporter.progress().runs.empty(); ++i) { // let the first lines in; the job keeps running
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        app.exporter.update(app.log, setup_error);
    }
    const rv_editor_probe_record setup = draw_tile(gui, app, wide);
    RV_EDITOR_CHECK(rv_editor_probe_compare(idle, setup) == "");
    std::string error;
    app.exporter.cancel(error);
    settle(app, rv_editor_build_state::cancelled);
    std::error_code ec;
    fs::remove_all(base, ec);
}

// The result buttons are always drawn.
RV_EDITOR_TEST(rv_editor_test_export_tile_result_buttons_always_present)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("buttons");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    const rv_editor_probe_record r = draw_tile(gui, app, wide);
    for (const char *b : result_buttons) {
        RV_EDITOR_CHECK(find(r, b) != nullptr);
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

// The result buttons are enabled only when they apply.
RV_EDITOR_TEST(rv_editor_test_export_tile_result_buttons_enabled_only_when_applicable)
{
    const auto ok = progress_with(rv_editor_step_state::done, rv_editor_step_state::done, rv_editor_run_end::ok);
    const auto bad = progress_with(rv_editor_step_state::failed, rv_editor_step_state::pending, rv_editor_run_end::failed);
    const auto run = progress_with(rv_editor_step_state::running, rv_editor_step_state::pending, rv_editor_run_end::open);
    const auto none =
        rv_editor_export_result_make(rv_editor_build_state::idle, "", "", rv_editor_progress{}, rv_editor_export_kind::web);
    const auto busy = rv_editor_export_result_make(rv_editor_build_state::building, "", "", run, rv_editor_export_kind::web);
    const auto cancelled = rv_editor_export_result_make(rv_editor_build_state::cancelled,
        "",
        "",
        rv_editor_progress{},
        rv_editor_export_kind::web);
    for (const auto &v : { none, busy, cancelled }) {
        RV_EDITOR_CHECK(!v.open_folder && !v.open_browser && !v.show_problems);
    }
    const auto web =
        rv_editor_export_result_make(rv_editor_build_state::succeeded, "/p/web", "", ok, rv_editor_export_kind::web);
    RV_EDITOR_CHECK(web.open_browser && !web.open_folder && !web.show_problems);
    const auto disc =
        rv_editor_export_result_make(rv_editor_build_state::succeeded, "/p/d", "", ok, rv_editor_export_kind::disc);
    RV_EDITOR_CHECK(disc.open_folder && !disc.open_browser && !disc.show_problems);
    const auto failed = rv_editor_export_result_make(rv_editor_build_state::failed, "", "x", bad, rv_editor_export_kind::disc);
    RV_EDITOR_CHECK(failed.show_problems && !failed.open_folder && !failed.open_browser);
}

// The Directory field shows the default destination relative to the project root.
RV_EDITOR_TEST(rv_editor_test_export_tile_dest_shows_relative_path)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("dest");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    draw_tile(gui, app, wide);
    RV_EDITOR_CHECK(rv_editor_pane_export_dest_probe().shown == "export/mppcdisc");
    RV_EDITOR_CHECK(rv_editor_pane_export_dest_probe().tooltip == (base / "export" / "mppcdisc").string());
    std::error_code ec;
    fs::remove_all(base, ec);
}
