// Files, Assets, Findings and Test Case keep their blocks in place by state, wrap their labels and buttons, and show
// paths by their tail.

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

// --- Files --------------------------------------------------------------------------------------------------------

namespace
{

struct files_world {
    fs::path dir;
    rv_editor_app app;
    rv_editor_ui ui;
    std::vector<rv_editor_ui_request> requests;
    rv_editor_ui_port port{ ui, requests };
    static constexpr rv_editor_pane_id pane = 1;

    explicit files_world(const char *name)
        : dir(make_dir(name))
    {
        fs::create_directories(dir / "sub");
        std::ofstream(dir / "a.txt").put('x');
        std::string error;
        RV_EDITOR_CHECK(app.files.open(dir, error) == 0);
        app.project.open = true;
        app.project.root = dir;
    }
    ~files_world()
    {
        std::string error;
        (void)app.files.close(error);
        fs::remove_all(dir);
    }
    rv_editor_files_view &view()
    {
        return app.files_views[pane];
    }
    rv_editor_probe_record draw(rv_editor_test_imgui &gui, float w)
    {
        return rv_editor_probe_draw(
            gui,
            w,
            [&](rv_editor_probe_record &) {
                rv_editor_pane_files(app, pane, theme, port);
            },
            tall);
    }
};

} // namespace

// AC9: the dialog opens in its reserved place, so the tree does not move, whichever dialog and whatever it says.
RV_EDITOR_TEST(rv_editor_test_files_dialog_does_not_move_the_tree)
{
    load_texts();
    rv_editor_test_imgui gui;
    files_world w("files_dialog");
    using dialog = rv_editor_files_view::rv_editor_files_dialog;
    w.draw(gui, width);
    const rv_editor_probe_record closed = w.draw(gui, width);
    const std::vector<std::string> anchors = { "##well" };
    RV_EDITOR_CHECK(find_part(closed, "##well") != nullptr);

    w.view().dialog = dialog::new_file;
    w.view().target = w.dir;
    const rv_editor_probe_record asking = w.draw(gui, width);
    RV_EDITOR_CHECK(stable(closed, asking, anchors).empty());

    w.view().error = "a file of that name exists: " + long_words();
    RV_EDITOR_CHECK(stable(closed, w.draw(gui, width), anchors).empty());
    w.view().error.clear();

    w.view().dialog = dialog::remove;
    w.view().target = w.dir / "sub";
    for (int i = 0; i < 12; ++i) {
        w.view().doomed.push_back("file" + std::to_string(i) + ".txt");
    }
    w.view().doomed_total = 30;
    RV_EDITOR_CHECK(stable(closed, w.draw(gui, width), anchors).empty());
}

// AC8: the dialog's name field stays inside a narrow tile.
RV_EDITOR_TEST(rv_editor_test_files_dialog_name_field_fits_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    files_world w("files_field");
    using dialog = rv_editor_files_view::rv_editor_files_dialog;
    float em = 0.0f;
    gui.frame([&] {
        em = ImGui::GetFontSize();
    });
    const float narrow = em * 15.0f;
    w.view().dialog = dialog::new_file;
    w.view().target = w.dir;
    w.draw(gui, narrow);
    const rv_editor_probe_record r = w.draw(gui, narrow);
    const rv_editor_probe_entry *field = find(r, "##name");
    RV_EDITOR_CHECK(field != nullptr);
    if (field != nullptr) {
        RV_EDITOR_CHECK(field->x1 <= narrow);
    }
}

// AC8: the dialog's header says where by the end of the path, not by its head.
RV_EDITOR_TEST(rv_editor_test_files_dialog_header_shows_the_tail_of_the_path)
{
    load_texts();
    rv_editor_test_imgui gui;
    files_world w("files_header");
    using dialog = rv_editor_files_view::rv_editor_files_dialog;
    w.view().dialog = dialog::new_dir;
    w.view().target = w.dir / std::string(60, 'Q') / "deep" / std::string(30, 'Y');
    float em = 0.0f;
    gui.frame([&] {
        em = ImGui::GetFontSize();
    });
    const auto draw = [&] {
        return draw_and_count(gui, em * 15.0f, "QY", [&] {
            rv_editor_pane_files(w.app, w.pane, theme, w.port);
        });
    };
    draw();
    const auto counts = draw();
    RV_EDITOR_CHECK(counts[0] == 0);
    RV_EDITOR_CHECK(counts[1] > 0);
}

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
        { "##title", rv_editor_text("findings.capture_frame"), rv_editor_text("findings.record_finding") })
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
    const std::vector<std::string> anchors = { "##note", rv_editor_text("findings.passed") };
    app.findings.test_case = 0;
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
    app.findings.results = { "Passed: a", "Failed: b", "Passed: c" };
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
    app.findings.error = "cannot write: " + long_words();
    RV_EDITOR_CHECK(stable(none, case_draw(gui, app, width), anchors).empty());
}

// AC6: Passed, Failed and Blocked wrap in a narrow tile and are inside it; so is a long case title.
RV_EDITOR_TEST(rv_editor_test_test_case_buttons_and_titles_stay_in_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    case_open(app);
    float em = 0.0f;
    gui.frame([&] {
        em = ImGui::GetFontSize();
    });
    const float narrow = em * 10.0f;
    case_draw(gui, app, narrow);
    const rv_editor_probe_record r = case_draw(gui, app, narrow);
    for (const char *key : { "findings.passed", "findings.failed", "findings.blocked" }) {
        const rv_editor_probe_entry *e = find(r, rv_editor_text(key));
        RV_EDITOR_CHECK(e != nullptr);
        if (e != nullptr) {
            RV_EDITOR_CHECK(e->x1 <= narrow);
        }
    }
    RV_EDITOR_CHECK(collisions(r, narrow).empty());
}
