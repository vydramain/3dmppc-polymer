// Assets, Findings and Test Case keep their blocks in place by state. (Files and the narrow-tile checks of Test Case
// moved to the S15 tests: the tile has a minimum now, and Files asks in the message box.)

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "rv_editor_test_panes_stable.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::panes_stable;

// --- Assets -------------------------------------------------------------------------------------------------------

// AC9: Refresh is always there, in one place, with or without a selection.
RV_EDITOR_TEST(rv_editor_test_assets_refresh_is_present_without_a_selection)
{
    load_texts();
    rv_editor_test_imgui gui;
    const fs::path dir = make_dir("assets");
    fs::create_directories(dir / "gfx");
    std::ofstream(dir / "gfx" / "s13_hero.txt").put('x');
    rv_editor_app app;
    app.project.open = true;
    app.project.root = dir;
    rv_editor_ui ui;
    std::vector<rv_editor_ui_request> requests;
    rv_editor_ui_port port{ ui, requests };
    const auto draw = [&] {
        return rv_editor_probe_draw(
            gui,
            width,
            [&](rv_editor_probe_record &) {
                rv_editor_pane_assets(app, nullptr, theme, port);
            },
            tall);
    };
    draw();
    const rv_editor_probe_record none = draw();
    const std::string refresh = rv_editor_text("pane_assets.refresh");
    RV_EDITOR_CHECK(find(none, refresh) != nullptr);

    // Click the one asset's cell: down, then up, a frame each.
    const rv_editor_probe_entry *cell = find(none, "##cell");
    RV_EDITOR_CHECK(cell != nullptr);
    if (cell != nullptr) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((cell->x0 + cell->x1) / 2.0f, (cell->y0 + cell->y1) / 2.0f);
        draw();
        io.AddMouseButtonEvent(0, true);
        draw();
        io.AddMouseButtonEvent(0, false);
        draw();
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        const rv_editor_probe_record selected = draw();
        RV_EDITOR_CHECK(find(selected, refresh) != nullptr);
        RV_EDITOR_CHECK(stable(none, selected, { refresh, "##cell" }).empty());
    }
    fs::remove_all(dir);
}

// --- Findings and Test Case ---------------------------------------------------------------------------------------

namespace
{
rv_editor_probe_record findings_draw(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            rv_editor_pane_findings(app, theme);
        },
        tall);
}

rv_editor_probe_record case_draw(rv_editor_test_imgui &gui, rv_editor_app &app, float w)
{
    return rv_editor_probe_draw(
        gui,
        w,
        [&](rv_editor_probe_record &) {
            rv_editor_pane_test_case(app, theme);
        },
        tall);
}

void case_open(rv_editor_app &app)
{
    app.project.open = true;
    app.project.root = "/tmp/rv_s13_cases";
    rv_editor_listed_case c;
    c.title = "Title " + long_words();
    c.steps = "Step one. " + long_words();
    c.expected = "Expect one. " + long_words();
    app.findings.cases = { c, c };
    app.findings.test_case = -1;
}

} // namespace

// AC9: an error of Findings does not move the form.
RV_EDITOR_TEST(rv_editor_test_findings_error_keeps_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    app.project.open = true;
    app.project.root = "/tmp/rv_s13_findings";
    findings_draw(gui, app);
    const rv_editor_probe_record clean = findings_draw(gui, app);
    app.findings.error = "cannot write: " + long_words();
    const rv_editor_probe_record with_error = findings_draw(gui, app);
    RV_EDITOR_CHECK(stable(clean,
        with_error,
        { "##Title", rv_editor_text("findings.capture_frame"), rv_editor_text("findings.record_finding") })
            .empty());
}

// AC9: a picked case's Steps and Expected, the results and an error of Test Case do not move the note or the buttons.
RV_EDITOR_TEST(rv_editor_test_test_case_sections_keep_their_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    case_open(app);
    case_draw(gui, app, width);
    const rv_editor_probe_record none = case_draw(gui, app, width);
    const std::vector<std::string> anchors = { rv_editor_text("findings.passed") };
    app.findings.test_case = 0;
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
    app.findings.results = { "Passed: a", "Failed: b", "Passed: c" };
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
    app.findings.error = "cannot write: " + long_words();
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
}
