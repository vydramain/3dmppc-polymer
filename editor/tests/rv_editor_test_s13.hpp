// Helpers of the S13 tests: the states of Output, Runtime Log, Build Log, Terminal, Problems and Search as the plan lists
// them, and finders for the message box and the content area in a probe record.

#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "terminal/rv_editor_terminal.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s13
{

using namespace rv_editor;
namespace fs = std::filesystem;

inline constexpr rv_editor_pane_id pane = 1;
inline constexpr int hex_digits = 8;

inline const std::vector<rv_editor_pane_kind> &kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::output,
        rv_editor_pane_kind::runtime_log,
        rv_editor_pane_kind::build_log,
        rv_editor_pane_kind::terminal,
        rv_editor_pane_kind::problems,
        rv_editor_pane_kind::search };
    return all;
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::output:
        return "output";
    case rv_editor_pane_kind::runtime_log:
        return "runtime_log";
    case rv_editor_pane_kind::build_log:
        return "build_log";
    case rv_editor_pane_kind::terminal:
        return "terminal";
    case rv_editor_pane_kind::problems:
        return "problems";
    default:
        return "search";
    }
}

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

inline std::string words(size_t chars)
{
    std::string s;
    while (s.size() < chars) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

// A shell with the states of one pane kind, in the order of the plan's table. A state sets everything it needs, so
// running one again after the others gives the same screen; the pane is pane 1.
struct fixture {
    rv_editor_test_imgui &gui;
    rv_editor_pane_kind kind;
    rv_editor_shell shell;
    fs::path where;
    std::vector<std::function<void()>> states;

    fixture(rv_editor_test_imgui &g, rv_editor_pane_kind k)
        : gui(g)
        , kind(k)
        , where(fs::temp_directory_path() / (std::string("rv_editor_s13_") + name_of(k)))
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        fs::create_directories(where, ec);
        build_states();
    }
    ~fixture()
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        ::setenv("SHELL", "/bin/sh", 1);
    }
    fixture(const fixture &) = delete;
    fixture &operator=(const fixture &) = delete;

    rv_editor_app &app()
    {
        return shell.app;
    }

    void draw(float width = 600.0f, float height = 500.0f)
    {
        gui.frame(
            [&] {
                rv_editor_shell_pane(&shell, pane, kind, stable_tiles::theme);
            },
            width,
            height);
    }

    void open_project()
    {
        app().project.open = true;
        app().project.root = where;
        app().project.disc_id = "disc";
        app().project.cache_dir = where / "cache";
    }

    void build_states()
    {
        switch (kind) {
        case rv_editor_pane_kind::terminal:
            terminal_states();
            break;
        case rv_editor_pane_kind::problems:
            problems_states();
            break;
        case rv_editor_pane_kind::search:
            search_states();
            break;
        default:
            log_states();
            break;
        }
    }

    // Lines of every source, so each kind has some to show whatever source it keeps.
    void add_lines(int count)
    {
        for (int i = 0; i < count; ++i) {
            for (const auto source : { rv_editor_log_source::editor,
                     rv_editor_log_source::runtime,
                     rv_editor_log_source::build,
                     rv_editor_log_source::candidate }) {
                app().log.add(source, rv_editor_log_level::info, "line " + std::to_string(i) + " " + words(i == 3 ? 300 : 20));
            }
        }
    }

    // empty, lines, filter with hidden lines, exported.
    void log_states()
    {
        states = {
            [this] {
                open_project();
            },
            [this] {
                add_lines(30);
            },
            [this] {
                app().log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "boom");
                app().log.add(rv_editor_log_source::candidate, rv_editor_log_level::warning, "careful");
                std::snprintf(app().outputs[pane].search, sizeof(app().outputs[pane].search), "%s", "matches nothing at all");
            },
            [this] {
                app().outputs[pane].search[0] = '\0';
                app().outputs[pane].exported = (where / words(200)).string();
            },
        };
    }

    // no project, not asked, no matches, hits, truncated.
    void search_states()
    {
        states = {
            [this] {
                app().project.open = false;
            },
            [this] {
                open_project();
                app().project_search.searched.clear();
                app().project_search.result = {};
            },
            [this] {
                app().project_search.searched = "needle";
                app().project_search.result = {};
                app().project_search.result.files = 12;
            },
            [this] {
                auto &r = app().project_search.result;
                r = {};
                r.files = 12;
                for (int i = 0; i < 30; ++i) {
                    r.hits.push_back({ where / (i == 2 ? words(150) + ".cpp" : "src/a.cpp"),
                        static_cast<int32_t>(i + 1),
                        4,
                        i == 2 ? words(300) : "int needle = 0;" });
                }
            },
            [this] {
                auto &r = app().project_search.result;
                r.truncated = true;
                r.skipped_dirs = 4;
                r.skipped_files = 9;
            },
        };
    }

    // no project, empty, build failed, problems, building.
    void problems_states()
    {
        states = {
            [this] {
                app().project.open = false;
                app().build_alert = {};
                app().problems.clear();
            },
            [this] {
                open_project();
            },
            [this] {
                app().build_alert = { rv_editor_build_alert_kind::failed, words(300) };
            },
            [this] {
                app().build_alert = {};
                app().problems.clear();
                app().problems.push_back({ where / "src/main.cpp", 3, 5, true, "expected ';'", "build" });
                app().problems.push_back({ where / (words(120) + ".cpp"), 90, 1, false, words(300), "build" });
            },
            [this] {
                if (!app().build.busy()) {
                    stable_tiles::start_busy_build(app(), where);
                }
            },
        };
    }

    // The shell of the terminal at pane 1 runs: its first frame starts it.
    void start_shell()
    {
        ::setenv("SHELL", "/bin/sh", 1);
        app().terminals[pane] = {};
        shell.closing_terminal = rv_editor_tile_none;
        for (int i = 0; i < 3; ++i) {
            draw();
        }
        RV_EDITOR_CHECK(app().terminals[pane].term != nullptr && app().terminals[pane].term->running());
    }

    // running, not started, ended, paste question, end question.
    void terminal_states()
    {
        states = {
            [this] {
                start_shell();
            },
            [this] {
                app().terminals[pane] = {};
                app().terminals[pane].term = std::make_unique<rv_editor_terminal>();
                app().terminals[pane].error = "no shell to start";
                shell.closing_terminal = rv_editor_tile_none;
            },
            [this] {
                ::setenv("SHELL", "/bin/true", 1);
                app().terminals[pane] = {};
                shell.closing_terminal = rv_editor_tile_none;
                for (int i = 0; i < 150; ++i) {
                    draw();
                    auto &view = app().terminals[pane];
                    std::string error;
                    if (view.term != nullptr) {
                        (void)view.term->update(error);
                    }
                    if (view.term != nullptr && !view.term->running()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                RV_EDITOR_CHECK(app().terminals[pane].term != nullptr && !app().terminals[pane].term->running());
            },
            [this] {
                start_shell();
                app().terminals[pane].paste = "first line\nsecond line " + words(300) + "\nthird line";
            },
            [this] {
                start_shell();
                shell.closing_terminal = pane;
            },
        };
    }
};

// The parts of a window's name ("test/##area_1A2B3C4D/##scroll_5E6F7A8B").
inline std::vector<std::string> parts_of(const std::string &label)
{
    std::vector<std::string> out;
    size_t from = 0;
    for (size_t at = label.find('/'); at != std::string::npos; at = label.find('/', from)) {
        out.push_back(label.substr(from, at - from));
        from = at + 1;
    }
    out.push_back(label.substr(from));
    return out;
}

// True when `part` is `prefix` + "_" + the eight hex digits ImGui gives a child's name.
inline bool is_child(const std::string &part, const char *prefix)
{
    const std::string want = std::string(prefix) + "_";
    return part.size() == want.size() + hex_digits && part.compare(0, want.size(), want) == 0;
}

// The message box's text window (rv_editor_message_box), nullptr when the record has none.
inline const rv_editor_probe_entry *message_box(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        const auto parts = parts_of(e.label);
        if (e.scroll_area && is_child(parts.back(), "##text")) {
            return &e;
        }
    }
    return nullptr;
}

// The frame of the content area (rv_editor_content_begin), nullptr when the record has none. The frame is the constant
// part: the scroll window inside it narrows by the bar's width when a bar appears.
inline const rv_editor_probe_entry *content_area(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (is_child(parts_of(e.label).back(), "##area")) {
            return &e;
        }
    }
    return nullptr;
}

// True for the scroll window inside a content area frame.
inline bool is_area_scroll(const rv_editor_probe_entry &e)
{
    const auto parts = parts_of(e.label);
    return parts.size() >= 2 && is_child(parts.back(), "##scroll") && is_child(parts[parts.size() - 2], "##area");
}

// A window of the old ask place (rv_editor_ask_place_begin) anywhere in the record.
inline bool has_ask_place(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        for (const auto &part : parts_of(e.label)) {
            if (is_child(part, "##ask")) {
                return true;
            }
        }
    }
    return false;
}

// Draws the pane of `f` once at `width` x `height` after a few settling frames and returns what it drew.
inline rv_editor_probe_record settled(fixture &f, float width, float height)
{
    rv_editor_probe_record record;
    for (int i = 0; i < 3; ++i) {
        record = rv_editor_probe_draw(
            f.gui,
            width,
            [&](rv_editor_probe_record &) {
                rv_editor_shell_pane(&f.shell, pane, f.kind, stable_tiles::theme);
            },
            height);
    }
    return record;
}

// "" when the two entries have the same rectangle; else a description.
inline std::string same_rect(const rv_editor_probe_entry *a, const rv_editor_probe_entry *b, const char *what)
{
    if (a == nullptr || b == nullptr) {
        return std::string(what) + " is missing";
    }
    if (a->x0 != b->x0 || a->y0 != b->y0 || a->x1 != b->x1 || a->y1 != b->y1) {
        return std::string(what) + " " + rv_editor_probe_detail::rect(*a) + " became " + rv_editor_probe_detail::rect(*b);
    }
    return "";
}

// "" when both records hold the same scroll windows (message box) and content area frames at the same rectangles; else
// the first difference. The scroll window inside a frame is not compared: it gives its width to the bar. The buttons of a
// message box differ by state and are not compared.
inline std::string windows_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto one_way = [](const rv_editor_probe_record &x, const rv_editor_probe_record &y, const char *where) {
        for (const auto &ex : x.entries) {
            const bool frame = is_child(parts_of(ex.label).back(), "##area");
            if ((!ex.scroll_area && !frame) || is_area_scroll(ex)) {
                continue;
            }
            const rv_editor_probe_entry *found = nullptr;
            for (const auto &ey : y.entries) {
                found = found == nullptr && (ey.scroll_area || frame) && ey.label == ex.label ? &ey : found;
            }
            if (found == nullptr) {
                return ex.label + " is missing from the " + where + " draw";
            }
            if (const std::string moved = same_rect(&ex, found, ex.label.c_str()); !moved.empty()) {
                return moved;
            }
        }
        return std::string();
    };
    const std::string d = one_way(a, b, "second");
    return d.empty() ? one_way(b, a, "first") : d;
}

} // namespace s13

} // namespace rv_editor_test
