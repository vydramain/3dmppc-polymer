// The start pages and Settings, Help and the Manual in a tile: path fields show the path whole, forms adapt to the
// tile's width, and nothing appears or moves by state (a problem or an error line, the inline browser, a note).

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_project_directory.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float tall = 3000.0f; // tall enough that a page never scrolls, so a scroll bar never changes the width
constexpr float roomy = 700.0f; // wide enough for Settings' label column
constexpr float narrow_em = 15.0f;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

bool contains(const std::string &s, const std::string &part)
{
    return s.find(part) != std::string::npos;
}

// 150 characters ending in "leaf_dir".
std::string long_path()
{
    std::string path = "/home/user";
    while (path.size() < 140) {
        path += "/deeply_nested_directory";
    }
    return path + "/leaf_dir";
}

std::string long_text()
{
    std::string s;
    while (s.size() < 300) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

// A real directory tree of long names under the temporary directory, deep enough that its path does not fit.
fs::path deep_dir(const char *name)
{
    fs::path dir = fs::temp_directory_path() / (std::string("rv_editor_test_start_") + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (int i = 0; i < 5; ++i) {
        dir /= "a_rather_long_directory_name_" + std::to_string(i);
    }
    fs::create_directories(dir, ec);
    return dir;
}

void remove_deep(const char *name)
{
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / (std::string("rv_editor_test_start_") + name), ec);
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (contains(e.label, label)) {
            return &e;
        }
    }
    return nullptr;
}

// "" when every item outside the scroll areas ends inside `width`, else the first that does not.
std::string inside(const rv_editor_probe_record &r, float width)
{
    for (const auto &e : r.entries) {
        if (!e.in_scroll && e.x1 > width + 0.5f) {
            return rv_editor_probe_detail::name(e) + " " + rv_editor_probe_detail::rect(e) + " reaches past " +
                std::to_string(width);
        }
    }
    return "";
}

template <class F>
rv_editor_probe_record draw(rv_editor_test_imgui &gui, float width, F page)
{
    rv_editor_probe_record r = rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            page();
        },
        tall);
    // ImGui's implicit "Debug" window is recorded before the test window; it is not the page.
    for (size_t i = 0; i < r.entries.size(); ++i) {
        if (r.entries[i].label == "test") {
            r.entries.erase(r.entries.begin(), r.entries.begin() + static_cast<std::ptrdiff_t>(i + 1));
            break;
        }
    }
    return r;
}

// The narrowest a tile of `kind` is: its minimum, or the old narrow width when the kind has no minimum of its own yet.
float narrow_width(rv_editor_test_imgui &gui, rv_editor_pane_kind kind)
{
    float narrow = 0.0f;
    gui.frame([&] {
        narrow = ImGui::GetFontSize() * narrow_em;
    });
    return std::max(narrow, stable_tiles::pane_minimum(gui, kind).w);
}

// --- New Project ---------------------------------------------------------------------------------------------------

// A form with a blank name: Create is off but no problem is written under a field.
void new_blank(rv_editor_shell &shell, const fs::path &dir)
{
    shell.start = {};
    std::snprintf(shell.start.dir, sizeof(shell.start.dir), "%s", dir.c_str());
}

// A form with a name and an id that cannot be one: the problem is written under the Disc ID field.
void new_problem(rv_editor_shell &shell, const fs::path &dir)
{
    new_blank(shell, dir);
    std::snprintf(shell.start.name, sizeof(shell.start.name), "Game");
    std::snprintf(shell.start.id, sizeof(shell.start.id), "1bad");
}

rv_editor_probe_record new_draw(rv_editor_test_imgui &gui, rv_editor_shell &shell, float width)
{
    return draw(gui, width, [&] {
        rv_editor_page_new_project(shell, theme);
    });
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_start_new_directory_shows_the_whole_path)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const std::string path = long_path();
    std::snprintf(shell.start.dir, sizeof(shell.start.dir), "%s", path.c_str());
    new_draw(gui, shell, 400);
    const rv_editor_path_probe &probe = rv_editor_page_new_project_dir_probe();
    RV_EDITOR_CHECK(probe.shown == path); // whole, no "..."
    RV_EDITOR_CHECK(!contains(probe.shown, "..."));
    RV_EDITOR_CHECK(probe.tooltip == path);
}

// The record without what is inside a content area and the area's scroll window: a wide description gives the scroll window
// a bar and a new height, which is not a form item moving.
rv_editor_probe_record outside_areas(const rv_editor_probe_record &r)
{
    rv_editor_probe_record out = r;
    out.entries.clear();
    for (const auto &e : r.entries) {
        if (!e.in_scroll && !s13::is_area_scroll(e)) {
            out.entries.push_back(e);
        }
    }
    return out;
}

RV_EDITOR_TEST(rv_editor_test_start_new_problem_does_not_move_the_buttons)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const fs::path dir = fs::temp_directory_path();
    new_blank(shell, dir);
    const auto without = new_draw(gui, shell, 500);
    new_problem(shell, dir);
    const auto with = new_draw(gui, shell, 500);
    // Create, Reset and Browse are items of the form: none moves, appears or resizes.
    RV_EDITOR_CHECK(rv_editor_probe_compare(outside_areas(without), outside_areas(with)).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_new_error_does_not_move_the_buttons)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    new_blank(shell, fs::temp_directory_path());
    const auto without = new_draw(gui, shell, 500);
    shell.start.error = long_text();
    const auto with = new_draw(gui, shell, 500);
    RV_EDITOR_CHECK(rv_editor_probe_compare(outside_areas(without), outside_areas(with)).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_new_form_fits_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    new_problem(shell, fs::temp_directory_path());
    shell.start.error = long_text();
    const float narrow = narrow_width(gui, rv_editor_pane_kind::new_project);
    RV_EDITOR_CHECK(inside(new_draw(gui, shell, narrow), narrow).empty());
}

// --- Open Project and its browser ----------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_start_open_browser_path_shows_the_whole_path)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const fs::path dir = deep_dir("open_tail");
    rv_editor_browser_start(shell.open_browser, "Choose", "Open Project", rv_editor_browse_pick::directory, dir);
    draw(gui, 300, [&] {
        rv_editor_page_open_project(shell, theme);
    });
    const rv_editor_path_probe &probe = rv_editor_browser_path_probe();
    RV_EDITOR_CHECK(probe.shown == dir.string()); // whole, no "..."
    RV_EDITOR_CHECK(!contains(probe.shown, "..."));
    RV_EDITOR_CHECK(probe.tooltip == dir.string());
    remove_deep("open_tail");
}

RV_EDITOR_TEST(rv_editor_test_start_open_long_path_stays_inside_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const fs::path dir = deep_dir("open_narrow");
    rv_editor_browser_start(shell.open_browser,
        "Choose a game directory",
        "Open Project",
        rv_editor_browse_pick::directory,
        dir);
    shell.open_browser.selected = dir; // the browser shows the selected path under the list
    const float narrow = narrow_width(gui, rv_editor_pane_kind::open_project);
    const auto r = draw(gui, narrow, [&] {
        rv_editor_page_open_project(shell, theme);
    });
    RV_EDITOR_CHECK(inside(r, narrow).empty());
    remove_deep("open_narrow");
}

// --- Settings ------------------------------------------------------------------------------------------------------

namespace
{

rv_editor_probe_record settings_draw(rv_editor_test_imgui &gui, rv_editor_shell &shell, float width)
{
    return draw(gui, width, [&] {
        rv_editor_page_settings(shell, theme);
    });
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_start_settings_override_path_shows_the_whole_path)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, 400); // loads the draft; a loaded page keeps what a test then sets
    const std::string path = long_path();
    std::snprintf(shell.settings_paths[0], sizeof(shell.settings_paths[0]), "%s", path.c_str());
    settings_draw(gui, shell, 400);
    const rv_editor_path_probe &probe = rv_editor_page_settings_path_probe(0);
    RV_EDITOR_CHECK(probe.shown == path); // whole, no "..."
    RV_EDITOR_CHECK(!contains(probe.shown, "..."));
    RV_EDITOR_CHECK(probe.tooltip == path);
}

RV_EDITOR_TEST(rv_editor_test_start_settings_browse_switches_the_page_mode_and_the_form_comes_back_unchanged)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto closed = settings_draw(gui, shell, roomy);
    const fs::path dir = deep_dir("settings_mode");
    rv_editor_browser_start(shell.settings_browser, "Select executable", "Select", rv_editor_browse_pick::executable, dir);
    shell.settings_browse = 0;
    const auto open = settings_draw(gui, shell, roomy);
    // The browser's path field is drawn (its probe is updated to the browser's directory) ...
    RV_EDITOR_CHECK(rv_editor_browser_path_probe().tooltip == dir.string());
    // ... it brings items of its own, and the form's items are gone (the tool path field among them).
    size_t kept_ids = 0, added = 0;
    for (const auto &e : open.entries) {
        bool in_closed = false;
        for (const auto &c : closed.entries) {
            in_closed = in_closed || c.id == e.id;
        }
        (in_closed ? kept_ids : added)++;
    }
    RV_EDITOR_CHECK(added > 0);
    RV_EDITOR_CHECK(kept_ids < closed.entries.size());
    RV_EDITOR_CHECK(inside(open, roomy).empty());
    shell.settings_browse = -1;
    const auto back = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(closed, back).empty());
    remove_deep("settings_mode");
}

RV_EDITOR_TEST(rv_editor_test_start_settings_error_line_does_not_move_what_is_below)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto without = settings_draw(gui, shell, roomy);
    shell.settings_error = long_text();
    const auto with = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(without, with).empty());
}

// The next three put a short note on a line that exists already, so no rect moves today; they guard that and the
// narrow-tile test below covers the note's width.
RV_EDITOR_TEST(rv_editor_test_start_settings_searching_line_does_not_move_what_is_below)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto without = settings_draw(gui, shell, roomy);
    shell.settings_apply_request = true;
    const auto with = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(without, with).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_settings_pace_note_does_not_move_by_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto without = settings_draw(gui, shell, roomy);
    shell.vsync_active = true; // the frame rate limit is off: a note says so
    const auto with = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(without, with).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_settings_not_applied_note_does_not_move_by_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto without = settings_draw(gui, shell, roomy);
    shell.settings_checks[0] = rv_editor_tool{}; // a Check ran: "not yet applied" follows the status
    const auto with = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(without, with).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_settings_automatic_hint_does_not_appear_by_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    settings_draw(gui, shell, roomy);
    const auto automatic = settings_draw(gui, shell, roomy); // an empty field: "next to the editor"
    std::snprintf(shell.settings_paths[0], sizeof(shell.settings_paths[0]), "/usr/bin/tool");
    const auto set = settings_draw(gui, shell, roomy);
    RV_EDITOR_CHECK(rv_editor_probe_compare(automatic, set).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_settings_fits_a_narrow_tile_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const float narrow = narrow_width(gui, rv_editor_pane_kind::settings);
    settings_draw(gui, shell, narrow);
    shell.settings_error = long_text();
    shell.settings_apply_request = true;
    shell.vsync_active = true;
    shell.settings_checks[0] = rv_editor_tool{};
    RV_EDITOR_CHECK(inside(settings_draw(gui, shell, narrow), narrow).empty());
}

// --- Help ----------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_start_help_filter_fits_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const float narrow = narrow_width(gui, rv_editor_pane_kind::help);
    const auto r = draw(gui, narrow, [&] {
        rv_editor_page_help(shell, theme);
    });
    RV_EDITOR_CHECK(inside(r, narrow).empty());
}

RV_EDITOR_TEST(rv_editor_test_start_help_no_match_keeps_a_line_in_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const auto all = draw(gui, 500, [&] {
        rv_editor_page_help(shell, theme);
    });
    std::snprintf(shell.help_filter, sizeof(shell.help_filter), "zzzz");
    const auto none = draw(gui, 500, [&] {
        rv_editor_page_help(shell, theme);
    });
    const rv_editor_probe_entry *filter_all = find(all, "##filter");
    const rv_editor_probe_entry *filter_none = find(none, "##filter");
    RV_EDITOR_CHECK(filter_all != nullptr && filter_none != nullptr);
    if (filter_all == nullptr || filter_none == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(filter_all->y0 == filter_none->y0 && filter_all->x1 == filter_none->x1);
    // Nothing matches: a "no match" line is an item below the filter (a message line has a rect of its own).
    bool line_below = false;
    for (const auto &e : none.entries) {
        line_below = line_below || (e.y0 >= filter_none->y1 && e.y1 > e.y0);
    }
    RV_EDITOR_CHECK(line_below);
}

// --- Manual --------------------------------------------------------------------------------------------------------

// A guard: headings and paragraphs already wrap, so this passes today.
RV_EDITOR_TEST(rv_editor_test_start_manual_stays_inside_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    const float narrow = std::max(120.0f, stable_tiles::pane_minimum(gui, rv_editor_pane_kind::manual).w);
    const auto r = draw(gui, narrow, [&] {
        rv_editor_page_manual(shell, theme);
    });
    RV_EDITOR_CHECK(inside(r, narrow).empty());
}

RV_EDITOR_TEST(rv_editor_test_interactive_manual_tracks_the_created_tile)
{
    rv_editor_shell shell;
    rv_editor_shell_load_layouts(shell, {}, "code");
    shell.app.project.open = true;
    using step = rv_editor_manual_step;
    shell.manual.step = step::new_tile;

    rv_editor_shell_new_tile(shell, rv_editor_pane_kind::files);
    RV_EDITOR_CHECK(shell.manual.step == step::new_tile);
    rv_editor_shell_new_tile(shell, rv_editor_pane_kind::output);
    RV_EDITOR_CHECK(shell.manual.step == step::close_tile);
    const auto added = shell.manual.pane;
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, added) != rv_editor_tile_none);
    rv_editor_shell_switch(shell, rv_editor_layout_preset::scene);
    rv_editor_shell_manual_update(shell);
    RV_EDITOR_CHECK(shell.manual.step == step::close_tile);
    rv_editor_shell_switch(shell, rv_editor_layout_preset::code);
    RV_EDITOR_CHECK(rv_editor_tile_remove(shell.ws.layout, added) == 0);
    rv_editor_shell_manual_update(shell);
    RV_EDITOR_CHECK(shell.manual.step == step::complete);
}

RV_EDITOR_TEST(rv_editor_test_interactive_manual_open_waits_for_a_project)
{
    load_texts();
    project_directory directory;
    rv_editor_shell shell;
    RV_EDITOR_CHECK(!directory.root.empty());
    if (directory.root.empty()) {
        return;
    }
    using step = rv_editor_manual_step;
    shell.manual.step = step::open_picker;
    rv_editor_shell_open_project(shell);
    RV_EDITOR_CHECK(shell.manual.step == step::pick_project);

    rv_editor_shell_request_open(shell, directory.root / "missing");
    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(shell.manual.step == step::pick_project);

    const fs::path project = directory.project("manual");
    rv_editor_shell_request_open(shell, project);
    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(shell.app.project.open && shell.app.project.root == project);
    RV_EDITOR_CHECK(shell.manual.step == step::complete);
}
