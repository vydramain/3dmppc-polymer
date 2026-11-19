// Project, Files, Assets, Run Config, Findings and Test Case keep their blocks in place by state, wrap their labels
// and buttons, and show paths by their tail: probe pairs (one width, two states), label/field rects, button rows in a
// narrow tile, and the glyphs a path field draws.

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

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float width = 400.0f;
constexpr float tall = 900.0f; // tall enough for a whole pane: the test window never clips or reorders it

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// A fresh directory for one test, removed by the caller.
fs::path make_dir(const char *name)
{
    const fs::path dir =
        fs::temp_directory_path() / (std::string("rv_editor_test_s13_") + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string long_words()
{
    std::string s;
    while (s.size() < 200) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

const rv_editor_probe_entry *find_part(const rv_editor_probe_record &r, const std::string &part)
{
    for (const auto &e : r.entries) {
        if (e.label.find(part) != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

// "" when every named item both draws have keeps its rectangle, every anchor is in both, and the content height is
// the same. Items that only one draw has (the state's own text) are not compared.
std::string stable(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const std::vector<std::string> &anchors)
{
    using namespace rv_editor_probe_detail;
    for (const std::string &anchor : anchors) {
        if (find_part(a, anchor) == nullptr) {
            return "'" + anchor + "' is missing from the first draw";
        }
        if (find_part(b, anchor) == nullptr) {
            return "'" + anchor + "' is missing from the second draw";
        }
    }
    for (const auto &ea : a.entries) {
        if (ea.id == 0 || ea.label.empty()) {
            continue;
        }
        for (const auto &eb : b.entries) {
            if (eb.id == ea.id && (ea.x0 != eb.x0 || ea.y0 != eb.y0 || ea.x1 != eb.x1 || ea.y1 != eb.y1)) {
                return name(ea) + " " + rect(ea) + " became " + rect(eb);
            }
        }
    }
    if (a.content_height != b.content_height) {
        return "content height " + std::to_string(a.content_height) + " became " + std::to_string(b.content_height);
    }
    return "";
}

// "" when the field of a label of `text` is clear of it: beside it, past its text, or under it, at the form's left edge
// (the label is text, which the probe does not record, so its width is measured).
std::string
label_clear(rv_editor_test_imgui &gui, const rv_editor_probe_record &r, const char *field_label, const std::string &text)
{
    const rv_editor_probe_entry *field = find(r, field_label);
    const rv_editor_probe_entry *left = find_part(r, "##args");
    if (field == nullptr || left == nullptr) {
        return std::string("no field ") + field_label;
    }
    float text_width = 0.0f;
    gui.frame([&] {
        text_width = ImGui::CalcTextSize(text.c_str()).x;
    });
    const bool beside = field->x0 >= left->x0 + text_width;
    const bool under = field->x0 <= left->x0 + 1.0f;
    return beside || under ? "" : std::string(field_label) + " starts at " + std::to_string(field->x0) + " over its label";
}

// "" when no two items overlap unless one holds the other (a container and what it contains), and every item is inside
// the window's width.
std::string collisions(const rv_editor_probe_record &r, float window_width)
{
    using namespace rv_editor_probe_detail;
    // A container holds its item with a few pixels of slack: rows and buttons overhang a well by their frame padding.
    const auto holds = [](const rv_editor_probe_entry &o, const rv_editor_probe_entry &i) {
        constexpr float slack = 3.0f;
        return o.x0 <= i.x0 + slack && o.y0 <= i.y0 + slack && o.x1 >= i.x1 - slack && o.y1 >= i.y1 - slack;
    };
    // The pane's items follow the host window "test"; what ImGui draws before it (Debug##Default) is not the pane's.
    size_t first = 0;
    while (first < r.entries.size() && r.entries[first].label != "test") {
        ++first;
    }
    first = first == r.entries.size() ? 0 : first;
    for (size_t i = first; i < r.entries.size(); ++i) {
        const auto &a = r.entries[i];
        if (a.x1 > window_width) {
            return name(a) + " " + rect(a) + " reaches beyond the tile";
        }
        for (size_t j = i + 1; j < r.entries.size(); ++j) {
            const auto &b = r.entries[j];
            const bool overlap = a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
            if (overlap && !holds(a, b) && !holds(b, a)) {
                return name(a) + " " + rect(a) + " overlaps " + name(b) + " " + rect(b);
            }
        }
    }
    return "";
}

// The pane drawn for one frame, then the count of `chars` drawn by it.
template <class F>
std::vector<int> draw_and_count(rv_editor_test_imgui &gui, float w, const std::string &chars, F body)
{
    std::vector<ImVec2> uv(chars.size());
    gui.frame(
        [&] {
            body();
            for (size_t i = 0; i < chars.size(); ++i) {
                const ImFontGlyph *g = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(chars[i]));
                uv[i] = ImVec2(g->U0, g->V0);
            }
        },
        w,
        tall);
    std::vector<int> counts(chars.size(), 0);
    for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : window->DrawList->VtxBuffer) {
            for (size_t i = 0; i < chars.size(); ++i) {
                if (std::abs(v.uv.x - uv[i].x) < 1e-6f && std::abs(v.uv.y - uv[i].y) < 1e-6f) {
                    ++counts[i];
                }
            }
        }
    }
    return counts;
}

// --- Run Config ---------------------------------------------------------------------------------------------------

uint64_t revision = 5000; // the pane refills its form when this moves; one per draw keeps tests apart

void run_open(rv_editor_app &app, const fs::path &root)
{
    app.project.open = true;
    app.project.root = root;
    app.run_config = {};
    app.run_config_revision = ++revision;
}

rv_editor_probe_record run_draw(rv_editor_test_imgui &gui, rv_editor_app &app, float w)
{
    app.run_config_revision = ++revision;
    return rv_editor_probe_draw(
        gui,
        w,
        [&](rv_editor_probe_record &) {
            rv_editor_pane_run_config(app, theme);
        },
        tall);
}

// A runtime file, a working directory and a memory card in a directory under `top`, so no problem line stands in for
// the field; the tails are Z, J and X, the head Q.
struct path_world {
    fs::path top;
    fs::path inner; // top / QQQ...
    std::string runtime, cwd, memcard;
    explicit path_world(const char *name)
        : top(make_dir((std::string("QQQQQQQQ_") + name).c_str()))
    {
        inner = top / std::string(60, 'Q');
        fs::create_directories(inner / "deep");
        runtime = (inner / "deep" / std::string(30, 'Z')).string();
        std::ofstream(runtime).put('x');
        cwd = (inner / "deep" / std::string(30, 'J')).string();
        fs::create_directories(cwd);
        memcard = (inner / "deep" / std::string(30, 'X')).string();
    }
    ~path_world()
    {
        fs::remove_all(top);
    }
};

void run_set_paths(rv_editor_app &app, const path_world &w)
{
    app.run_config.profiles[0].runtime = w.runtime;
    app.run_config.profiles[0].cwd = w.cwd;
    app.run_config.profiles[0].memcard = w.memcard;
}

// Every path field, unfocused, shows the end of its path ('Z', 'J', 'X'); read from the field's probe. Outside the root
// (`root` empty) the shown text is "..." + the end of the path, shorter than the path; inside it the path is relative.
void check_tails(rv_editor_test_imgui &gui, rv_editor_app &app, const std::string &root = {})
{
    run_draw(gui, app, 600.0f);
    const rv_editor_probe_record r = run_draw(gui, app, 600.0f);
    const std::pair<const char *, char> fields[] = { { "##Runtime", 'Z' }, { "##Working dir", 'J' }, { "##Memory card", 'X' } };
    for (const auto &[label, tail] : fields) {
        const rv_editor_probe_entry *e = find(r, label);
        RV_EDITOR_CHECK(e != nullptr);
        const rv_editor_path_probe *p = e != nullptr ? rv_editor_path_probe_of(e->id) : nullptr;
        RV_EDITOR_CHECK(p != nullptr);
        if (p != nullptr) {
            RV_EDITOR_CHECK(!p->editing);
            RV_EDITOR_CHECK(!p->shown.empty() && p->shown.back() == tail);
            RV_EDITOR_CHECK(p->shown.size() < p->tooltip.size());
            RV_EDITOR_CHECK(p->tooltip.back() == tail);
            if (root.empty()) {
                RV_EDITOR_CHECK(p->shown.rfind("...", 0) == 0);
            } else {
                RV_EDITOR_CHECK(p->shown.rfind(root, 0) != 0 && p->tooltip.rfind(root, 0) == 0);
            }
        }
    }
}

} // namespace

// AC5: a label of 30 characters never covers its field, nor leaves the tile.
RV_EDITOR_TEST(rv_editor_test_run_config_long_label_does_not_meet_its_field)
{
    const fs::path dir = make_dir("run_label");
    const fs::path file = dir / "texts.toml";
    std::ofstream(file) << "[pane_run_config]\nname_label = \"NameNameNameNameNameNameNameNa\"\n"
                           "runtime_label = \"RuntimeRuntimeRuntimeRuntimeRu\"\n"
                           "working_dir_label = \"WorkingWorkingWorkingWorkingWo\"\n"
                           "memory_card_label = \"MemoryMemoryMemoryMemoryMemory\"\n";
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load(file, error) == 0);
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, dir);
    run_draw(gui, app, width);
    const rv_editor_probe_record r = run_draw(gui, app, width);
    RV_EDITOR_CHECK(label_clear(gui, r, "##name", "NameNameNameNameNameNameNameNa").empty());
    RV_EDITOR_CHECK(label_clear(gui, r, "##Runtime", "RuntimeRuntimeRuntimeRuntimeRu").empty());
    RV_EDITOR_CHECK(label_clear(gui, r, "##Working dir", "WorkingWorkingWorkingWorkingWo").empty());
    load_texts();
    fs::remove_all(dir);
}

// AC5: the two checkboxes of the form do not overlap and stay in the tile.
RV_EDITOR_TEST(rv_editor_test_run_config_checkboxes_stay_in_the_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, "/tmp");
    run_draw(gui, app, width);
    const rv_editor_probe_record r = run_draw(gui, app, width);
    RV_EDITOR_CHECK(collisions(r, width).empty());
}

// AC6: Apply, Apply and Restart and Revert in a 15em tile wrap and are inside it.
RV_EDITOR_TEST(rv_editor_test_run_config_buttons_wrap_in_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, "/tmp");
    float em = 0.0f;
    gui.frame([&] {
        em = ImGui::GetFontSize();
    });
    const float narrow = em * 15.0f;
    run_draw(gui, app, narrow);
    const rv_editor_probe_record r = run_draw(gui, app, narrow);
    for (const char *key :
        { "pane_run_config.apply_label", "pane_run_config.apply_restart_label", "pane_run_config.revert_label" }) {
        const rv_editor_probe_entry *e = find(r, rv_editor_text(key));
        RV_EDITOR_CHECK(e != nullptr);
        if (e != nullptr) {
            RV_EDITOR_CHECK(e->x0 >= 0.0f);
            RV_EDITOR_CHECK(e->x1 <= narrow);
        }
    }
}

// AC8: outside the project root the fields show the end of the path.
RV_EDITOR_TEST(rv_editor_test_run_config_paths_show_their_tail)
{
    load_texts();
    rv_editor_test_imgui gui;
    path_world world("run_tail");
    const fs::path other = make_dir("run_tail_root");
    rv_editor_app app;
    run_open(app, other);
    run_set_paths(app, world);
    check_tails(gui, app);
    fs::remove_all(other);
}

// AC8: inside the project root the fields show the path relative to it, so the root's own head is not drawn.
RV_EDITOR_TEST(rv_editor_test_run_config_paths_inside_the_root_are_relative)
{
    load_texts();
    rv_editor_test_imgui gui;
    path_world world("run_rel");
    rv_editor_app app;
    run_open(app, world.inner);
    run_set_paths(app, world);
    check_tails(gui, app, world.inner.string());
}

// AC9: a profile error, a problem of the edited profile do not move the form or the buttons.
RV_EDITOR_TEST(rv_editor_test_run_config_error_and_problem_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, "/tmp");
    run_draw(gui, app, width);
    const rv_editor_probe_record clean = run_draw(gui, app, width);
    const std::vector<std::string> anchors = { "##name", rv_editor_text("pane_run_config.apply_label") };
    app.run_config.error = "project.toml: " + long_words();
    const rv_editor_probe_record with_error = run_draw(gui, app, width);
    RV_EDITOR_CHECK(stable(clean, with_error, anchors).empty());
    app.run_config.error.clear();
    app.run_config.profiles[0].runtime = "/nonexistent/" + std::string(80, 'r');
    const rv_editor_probe_record with_problem = run_draw(gui, app, width);
    RV_EDITOR_CHECK(stable(clean, with_problem, anchors).empty());
}

// --- Project ------------------------------------------------------------------------------------------------------

namespace
{

rv_editor_probe_record project_draw(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            rv_editor_pane_project(app, theme);
        },
        tall);
}

void project_open(rv_editor_app &app)
{
    app.project.open = true;
    app.project.root = "/tmp/rv_s13_project";
    app.project.disc_id = "disc";
}

} // namespace

// AC9: a manifest error, a tool's problem and a settings error do not move what follows them.
RV_EDITOR_TEST(rv_editor_test_project_error_lines_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    project_open(app);
    project_draw(gui, app);
    const rv_editor_probe_record clean = project_draw(gui, app);
    const std::vector<std::string> anchors = { rv_editor_text("panes.look_for_tools_button") };
    app.project.manifest_error = "disc.toml:3: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
    app.project.manifest_error.clear();
    app.tools.burner.problem = "cannot run the burner: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
    app.tools.burner.problem.clear();
    app.tools.settings_error = "settings.toml:2: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
}

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
