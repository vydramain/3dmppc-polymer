// The Export tile v2: a kind dropdown over the kinds table with a constant description area, an always-present Web Tools
// block, a constant Progress block with result buttons always in place, and a tile that keeps every block in place by
// kind and by state.

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

using namespace rv_editor;
using namespace rv_editor_test;

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

namespace fs = std::filesystem;

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float wide = 420.0f;
constexpr float narrow = 230.0f;
constexpr const char *result_buttons[] = { "Open Folder", "Open in Browser", "Show in Problems" };
constexpr const char *action_buttons[] = { "Export", "Cancel" };

fs::path scratch(const char *name)
{
    const fs::path dir =
        fs::temp_directory_path() / (std::string("rv_editor_test_tile_") + name + "_" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// A burner stand-in running `body`; the export's output directory is $6.
fs::path script(const fs::path &where, const char *body)
{
    const fs::path file = where / "burner.sh";
    std::ofstream(file) << "#!/bin/sh\n" << body << "\n";
    chmod(file.c_str(), 0700);
    return file;
}

// An app with a stand-in project and burner.
void make_app(rv_editor_app &app, const fs::path &root, const fs::path &burner)
{
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    app.project.open = true;
    app.project.root = root;
    app.project.disc_id = "disc";
    app.project.cache_dir = root / "cache";
    app.tools.burner.path = burner;
    app.tools.baker.path = burner;
}

void settle(rv_editor_app &app, rv_editor_build_state want)
{
    for (int i = 0; i < 500 && app.exporter.state() != want; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string error;
        app.exporter.update(app.log, error);
    }
}

bool contains(const std::string &s, const char *part)
{
    return s.find(part) != std::string::npos;
}

// The entries that name a block, the kind control or a button (the contents of a block differ by state; its place may not).
bool placed(const rv_editor_probe_entry &e)
{
    if (contains(e.label, "##export_")) {
        return true;
    }
    for (const char *b : result_buttons) {
        if (contains(e.label, b)) {
            return true;
        }
    }
    for (const char *b : action_buttons) {
        if (e.label == b || contains(e.label, (std::string("/") + b).c_str())) {
            return true;
        }
    }
    return false;
}

rv_editor_probe_record placed_only(const rv_editor_probe_record &r)
{
    rv_editor_probe_record out;
    out.content_height = r.content_height;
    for (const auto &e : r.entries) {
        if (placed(e)) {
            out.entries.push_back(e);
        }
    }
    return out;
}

rv_editor_probe_record draw_tile(rv_editor_test_imgui &gui, rv_editor_app &app, float width)
{
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_pane_export(app, theme);
    };
    // Tall enough for the whole tile, so the result of the bottom block is inside the window clip rect.
    constexpr float height = 800.0f;
    rv_editor_probe_draw(gui, width, body, height); // an auto-sized child settles on the second frame
    return placed_only(rv_editor_probe_draw(gui, width, body, height));
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const char *part)
{
    for (const auto &e : r.entries) {
        if (contains(e.label, part)) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

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

// The whole tile is the same for Disc, Bundle and Web; Web Tools is always there.
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
        RV_EDITOR_CHECK(find(disc, "##export_block_web_tools") != nullptr);
        RV_EDITOR_CHECK(find(disc, "##export_block_progress") != nullptr);
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
        RV_EDITOR_CHECK(find(none, "##export_block_progress") != nullptr);
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

// Result buttons are always drawn in the Progress block, in the same place.
RV_EDITOR_TEST(rv_editor_test_export_tile_result_buttons_always_present)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path base = scratch("buttons");
    rv_editor_app app;
    make_app(app, base, script(base, "exit 0"));
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    const rv_editor_probe_record r = draw_tile(gui, app, wide);
    const rv_editor_probe_entry *progress = find(r, "##export_block_progress");
    RV_EDITOR_CHECK(progress != nullptr);
    for (const char *b : result_buttons) {
        const rv_editor_probe_entry *e = find(r, b);
        RV_EDITOR_CHECK(e != nullptr);
        if (e != nullptr && progress != nullptr) {
            RV_EDITOR_CHECK(e->x0 >= progress->x0 && e->x1 <= progress->x1 && e->y0 >= progress->y0 && e->y1 <= progress->y1);
        }
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
