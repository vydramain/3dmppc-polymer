// Helpers of the S19 tests: the states of Recent Projects, New Project, Open Project, Settings, Help and the manual as the
// plan's table lists them, and a few finders over a probe record. Recent Projects is a page of the start screen with no
// pane kind of its own; the other five are pane kinds drawn in a tile.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "project/rv_editor_templates.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s19
{

using namespace rv_editor;
namespace fs = std::filesystem;
using s13::content_area;
using s13::is_area_scroll;
using s13::is_child;
using s13::load_texts;
using s13::message_box;
using s13::parts_of;
using s13::words;

inline constexpr rv_editor_pane_id pane = 1;
inline constexpr int dirs_in_where = 14; // directories the browsers list: more than a list shows at the minimum

enum class view {
    recent,
    new_project,
    open_project,
    settings,
    help,
    manual
};

inline const std::vector<view> &views()
{
    static const std::vector<view> all = { view::recent,
        view::new_project,
        view::open_project,
        view::settings,
        view::help,
        view::manual };
    return all;
}

// The five that are pane kinds (a tile).
inline const std::vector<view> &tiles()
{
    static const std::vector<view> all = { view::new_project, view::open_project, view::settings, view::help, view::manual };
    return all;
}

inline const char *name_of(view v)
{
    switch (v) {
    case view::recent:
        return "recent";
    case view::new_project:
        return "new_project";
    case view::open_project:
        return "open_project";
    case view::settings:
        return "settings";
    case view::help:
        return "help";
    default:
        return "manual";
    }
}

inline rv_editor_pane_kind kind_of(view v)
{
    switch (v) {
    case view::new_project:
        return rv_editor_pane_kind::new_project;
    case view::open_project:
        return rv_editor_pane_kind::open_project;
    case view::settings:
        return rv_editor_pane_kind::settings;
    case view::help:
        return rv_editor_pane_kind::help;
    default:
        return rv_editor_pane_kind::manual;
    }
}

struct minimum {
    float w, h;
};

// The constant minimum of a view: the shell's for a tile, the page's own for Recent Projects. Inside a frame (it measures
// the font).
inline minimum minimum_of(rv_editor_test_imgui &gui, view v)
{
    if (v != view::recent) {
        const auto m = stable_tiles::pane_minimum(gui, kind_of(v));
        return { m.w, m.h };
    }
    minimum result{ 0, 0 };
    gui.frame([&] {
        const rv_editor_size m = rv_editor_recent_minimum();
        result = { static_cast<float>(m.w), static_cast<float>(m.h) };
    });
    return result;
}

struct state {
    std::function<void()> set;
    bool mode = false; // the state replaces the page's content (the browser as a card); its windows differ from the others'
};

// A shell with the states of one view, in the order of the plan's table. A state sets everything it needs, so running one
// again after the others gives the same screen.
struct fixture {
    rv_editor_test_imgui &gui;
    view which;
    rv_editor_shell shell;
    fs::path where;
    std::string old_xdg;
    bool had_xdg = false;
    std::vector<state> states;

    fixture(rv_editor_test_imgui &g, view v)
        : gui(g)
        , which(v)
        , where(fs::temp_directory_path() / (std::string("rv_editor_s19_") + name_of(v)))
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        fs::create_directories(where, ec);
        for (int i = 0; i < dirs_in_where; ++i) {
            fs::create_directories(where / ("dir_" + std::to_string(i) + "_" + words(40)), ec);
        }
        if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) {
            had_xdg = true;
            old_xdg = xdg;
        }
        ::setenv("XDG_CONFIG_HOME", (where / "cfg").c_str(), 1);
        build_states();
    }
    ~fixture()
    {
        if (had_xdg) {
            ::setenv("XDG_CONFIG_HOME", old_xdg.c_str(), 1);
        } else {
            ::unsetenv("XDG_CONFIG_HOME");
        }
        std::error_code ec;
        fs::remove_all(where, ec);
    }
    fixture(const fixture &) = delete;
    fixture &operator=(const fixture &) = delete;

    // The page as the shell draws it: Recent Projects by its own entry, the rest by the pane dispatch.
    void page()
    {
        if (which == view::recent) {
            rv_editor_page_recent(shell, stable_tiles::theme);
        } else {
            rv_editor_shell_pane(&shell, pane, kind_of(which), stable_tiles::theme);
        }
    }

    void draw(float width, float height)
    {
        gui.frame(
            [&] {
                page();
            },
            width,
            height);
    }

    // Three settling frames, then the recorded one.
    rv_editor_probe_record settled(float width, float height)
    {
        rv_editor_probe_record record;
        for (int i = 0; i < 3; ++i) {
            record = rv_editor_probe_draw(
                gui,
                width,
                [&](rv_editor_probe_record &) {
                    page();
                },
                height);
        }
        return record;
    }

    void build_states()
    {
        switch (which) {
        case view::recent:
            recent_states();
            break;
        case view::new_project:
            new_states();
            break;
        case view::open_project:
            open_states();
            break;
        case view::settings:
            settings_states();
            break;
        case view::help:
            help_states();
            break;
        default:
            states = { { [] {} } };
            break;
        }
    }

    // --- Recent Projects: the file of recent projects, the newest first.
    fs::path make_project(const std::string &name, bool with_disc)
    {
        const fs::path root = where / "projects" / name;
        std::error_code ec;
        fs::create_directories(root, ec);
        if (with_disc) {
            std::ofstream(root / "disc.toml") << "title = \"t\"\n";
        }
        return root;
    }

    void set_recents(const std::vector<fs::path> &roots)
    {
        std::error_code ec;
        fs::remove(where / "cfg" / "3dmppc-editor" / "recent", ec);
        for (auto at = roots.rbegin(); at != roots.rend(); ++at) {
            rv_editor_recent_add(*at);
        }
        shell.start_selected = roots.empty() ? fs::path() : roots.front();
    }

    // no recent projects, three, long names and paths, one that is not there any more.
    void recent_states()
    {
        states = {
            { [this] {
                set_recents({});
            } },
            { [this] {
                set_recents({ make_project("alpha", true), make_project("beta", true), make_project("gamma", true) });
            } },
            { [this] {
                set_recents({ make_project(std::string(200, 'n'), true),
                    make_project(words(100) + "/" + words(150), true),
                    make_project("gamma", true) });
            } },
            { [this] {
                set_recents({ make_project("alpha", true), make_project("gone", false), make_project("gamma", true) });
            } },
        };
    }

    // --- New Project
    void new_start(const std::string &name, const std::string &id, const fs::path &dir)
    {
        shell.start = {};
        std::snprintf(shell.start.name, sizeof(shell.start.name), "%s", name.c_str());
        std::snprintf(shell.start.id, sizeof(shell.start.id), "%s", id.c_str());
        std::snprintf(shell.start.dir, sizeof(shell.start.dir), "%s", dir.c_str());
    }

    // empty, valid, a bad id, a bad directory, a failed create, the directory browser.
    void new_states()
    {
        states = {
            { [this] {
                new_start("", "", where);
            } },
            { [this] {
                new_start("Game", "game", where);
            } },
            { [this] {
                new_start("Game", "1bad", where);
            } },
            { [this] {
                new_start("Game", "game", where / "missing" / words(60));
            } },
            { [this] {
                new_start("Game", "game", where);
                shell.start.error = words(300);
            } },
            { [this] {
                 new_start("Game", "game", where);
                 rv_editor_browser_start(shell.start.browser,
                     "Select directory for New Project",
                     "Select Directory",
                     rv_editor_browse_pick::directory,
                     where);
                 shell.start.browsing = true;
             },
                true },
        };
    }

    // --- Open Project
    void open_start(bool disc)
    {
        std::error_code ec;
        fs::remove(where / "disc.toml", ec);
        if (disc) {
            std::ofstream(where / "disc.toml") << "title = \"t\"\n";
        }
        rv_editor_browser_start(shell.open_browser,
            "Choose a game directory: the one holding its disc.toml",
            "Open Project",
            rv_editor_browse_pick::directory,
            where);
    }

    // a disc found, no disc, a directory that cannot be listed.
    void open_states()
    {
        states = {
            { [this] {
                open_start(true);
            } },
            { [this] {
                open_start(false);
            } },
            { [this] {
                open_start(false);
                shell.open_browser.dir = where / "gone" / words(100);
                shell.open_browser.path[0] = '\0';
            } },
        };
    }

    // --- Settings
    void settings_clear()
    {
        shell.settings_loaded = true;
        shell.settings_error.clear();
        shell.settings_apply_request = false;
        shell.settings_browse = -1;
        shell.vsync_active = false;
        for (auto &check : shell.settings_checks) {
            check.reset();
        }
        shell.app.tools.baker.problem.clear();
    }

    // loaded, a tool checked, a tool with a problem, searching, a save error, the executable browser.
    void settings_states()
    {
        states = {
            { [this] {
                settings_clear();
            } },
            { [this] {
                settings_clear();
                shell.settings_checks[0] = rv_editor_tool{};
            } },
            { [this] {
                settings_clear();
                shell.app.tools.baker.problem = words(200);
            } },
            { [this] {
                settings_clear();
                shell.settings_apply_request = true;
            } },
            { [this] {
                settings_clear();
                shell.settings_error = words(300);
            } },
            { [this] {
                 settings_clear();
                 rv_editor_browser_start(shell.settings_browser,
                     "Select executable for Bakers",
                     "Select",
                     rv_editor_browse_pick::executable,
                     where);
                 shell.settings_browse = 0;
             },
                true },
        };
    }

    // --- Help: no filter, a filter that keeps rows, a filter that keeps none.
    void help_filter(const char *text)
    {
        std::snprintf(shell.help_filter, sizeof(shell.help_filter), "%s", text);
    }

    void help_states()
    {
        states = {
            { [this] {
                help_filter("");
            } },
            { [this] {
                help_filter("save");
            } },
            { [this] {
                help_filter("zzzz");
            } },
        };
    }
};

} // namespace s19

} // namespace rv_editor_test
