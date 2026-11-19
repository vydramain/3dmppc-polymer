// Helpers of the S14 tests: the states of Runtime Controls, Release Controls, the Toolchest and Game as the plan's table
// lists them (one shell per state), and a one-tile workspace that is drawn the way the shell draws its tiles.

#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "platform/rv_editor_process.hpp"
#include "release/rv_editor_candidate.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s14
{

using namespace rv_editor;
namespace fs = std::filesystem;

inline constexpr rv_editor_pane_id strip_pane = 1;
inline constexpr rv_editor_theme theme = stable_tiles::theme;

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::controls:
        return "controls";
    case rv_editor_pane_kind::release_controls:
        return "release_controls";
    case rv_editor_pane_kind::toolchest:
        return "toolchest";
    default:
        return "game";
    }
}

inline const std::vector<rv_editor_pane_kind> &strip_kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::controls,
        rv_editor_pane_kind::release_controls,
        rv_editor_pane_kind::toolchest,
        rv_editor_pane_kind::game };
    return all;
}

// A console stand-in that answers status and never ends on quit: stop() then leaves the session hung after the grace.
inline bool start_hung_session(rv_editor_app &app, const fs::path &where)
{
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    std::error_code ec;
    fs::create_directories(where, ec);
    const std::string status = std::string("protocol=") + rv_editor_session::protocol_supported +
        " mode=running disc=64697363 disc_hash=abc pdk=1.0 medium=live entry_reloadable=1 entry_revision=1 entry_hash=h "
        "lua_used=10 lua_budget=1000";
    const fs::path script = where / "console_hung.sh";
    pane_state::write_script(script,
        "while read -r id verb rest; do\n"
        "  case \"$verb\" in\n"
        "    status) echo \"$id ok " +
            status +
            "\";;\n"
            "  esac\n"
            "done\n");
    rv_editor_session_launch launch;
    launch.console = script;
    launch.disc_dir = where / "discdir";
    launch.memcard = where / "card";
    launch.cwd = where;
    launch.build_number = 1;
    std::string error;
    if (app.session.start(launch, app.log, error) != RV_OK) {
        return false;
    }
    for (int i = 0; i < 300 && !(app.session.connected() && app.session.facts().revision >= 0); ++i) {
        std::string e;
        app.session.update(app.log, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // quit goes out, the stand-in ignores it, and the grace (three seconds) runs out.
    app.session.stop(app.log, error);
    for (int i = 0; i < 500 && !app.session.hung(); ++i) {
        std::string e;
        app.session.update(app.log, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return app.session.hung();
}

inline void script_build(rv_editor_app &app, const fs::path &where, const std::string &body)
{
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    static int count = 0; // one script per build: a running build must not see its script rewritten
    const fs::path script = where / ("burner" + std::to_string(count++) + ".sh");
    std::ofstream(script) << "#!/bin/sh\n" << body;
    chmod(script.c_str(), 0700);
    app.tools.burner.path = script;
    app.tools.baker.path = script;
    std::string error;
    RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, error) == RV_OK);
}

// A candidate with a big number and every check passed (`all`) or none run.
inline rv_editor_candidate candidate(uint32_t number, bool all_passed)
{
    rv_editor_candidate c;
    c.number = number;
    c.sha256 = "abc";
    c.checks = rv_editor_checks_make();
    if (all_passed) {
        for (size_t i = 0; i < c.checks.size(); ++i) {
            rv_editor_check_set(c, i, rv_editor_check_state::passed, "ok", "env");
        }
    }
    return c;
}

// One shell per state of a kind, in the order of the plan's table; names say which state.
struct states {
    rv_editor_pane_kind kind;
    fs::path where;
    std::vector<std::unique_ptr<rv_editor_shell>> shells;
    std::vector<std::string> names;
    size_t project_from = 0; // states before this have no project: only fits_whole applies to them

    explicit states(rv_editor_pane_kind k)
        : kind(k)
        , where(pane_state::fresh_dir((std::string("rv_editor_s14_") + name_of(k)).c_str()))
    {
        switch (k) {
        case rv_editor_pane_kind::controls:
            controls();
            break;
        case rv_editor_pane_kind::release_controls:
            release();
            break;
        case rv_editor_pane_kind::toolchest:
            toolchest();
            break;
        default:
            game();
            break;
        }
    }
    ~states()
    {
        for (auto &shell : shells) {
            std::string error;
            shell->app.session.shutdown(shell->app.log, error);
        }
        pane_state::remove_dir(where);
    }
    states(const states &) = delete;
    states &operator=(const states &) = delete;

    rv_editor_shell &add(const char *name, bool project = true)
    {
        shells.push_back(std::make_unique<rv_editor_shell>());
        names.push_back(name);
        if (project) {
            pane_state::open_project(shells.back()->app, where);
        }
        return *shells.back();
    }

    bool session(rv_editor_app &app, const char *sub, const pane_state::fake_console_options &o = {})
    {
        std::error_code ec;
        fs::create_directories(where / sub, ec);
        return pane_state::start_fake_session(app, where / sub, o);
    }

    void wait_failed(rv_editor_app &app)
    {
        std::string error;
        for (int i = 0; i < 200 && app.build.busy(); ++i) {
            app.build.update(app.log, error);
            usleep(10000);
        }
        RV_EDITOR_CHECK(app.build.state() == rv_editor_build_state::failed);
    }

    // no project, idle, building, failed, running, paused, hung (Force Stop), reload badge.
    void controls()
    {
        project_from = 1;
        add("no project", false);
        add("idle");
        auto &building = add("building").app;
        script_build(building,
            where,
            "echo 'mppcburner: [plan] a b c' >&2\necho 'mppcburner: [2/3] xxxxxxxxxxxxxxxx' >&2\nsleep 30\n");
        std::string error;
        for (int i = 0; i < 100 && building.build.progress().runs.empty(); ++i) {
            building.build.update(building.log, error);
            usleep(10000);
        }
        auto &failed = add("failed").app;
        script_build(failed, where, "exit 1\n");
        wait_failed(failed);
        RV_EDITOR_CHECK(session(add("running").app, "run"));
        pane_state::fake_console_options paused;
        paused.mode = "paused";
        RV_EDITOR_CHECK(session(add("paused").app, "pause", paused));
        RV_EDITOR_CHECK(start_hung_session(add("hung").app, where / "hung"));
        auto &badge = add("reload badge").app;
        RV_EDITOR_CHECK(session(badge, "badge"));
        badge.run_config.profiles[badge.run_config.active].reload_on_save = true;
    }

    // no project, no candidate, candidate, building, hung (Force Stop), all checks passed.
    void release()
    {
        project_from = 1;
        add("no project", false);
        add("no candidate");
        add("candidate").app.release.candidates.push_back(candidate(999999, false));
        auto &building = add("building").app;
        building.release.candidates.push_back(candidate(999999, false));
        script_build(building, where, "sleep 30\n");
        building.release.building = true;
        auto &hung = add("hung").app;
        hung.release.candidates.push_back(candidate(3, false));
        RV_EDITOR_CHECK(start_hung_session(hung, where / "hung"));
        add("all passed").app.release.candidates.push_back(candidate(3, true));
    }

    // debug without marks, with a short and with a long mark, scene tools, no scene.
    void toolchest()
    {
        add("debug, no mark").app.preset = rv_editor_layout_preset::debug;
        auto &short_mark = add("debug, a mark").app;
        short_mark.preset = rv_editor_layout_preset::debug;
        short_mark.marks.push_back("m");
        auto &long_mark = add("debug, a long mark").app;
        long_mark.preset = rv_editor_layout_preset::debug;
        long_mark.marks.push_back(std::string(160, 'm'));
        auto &scene = add("scene tools").app;
        scene.preset = rv_editor_layout_preset::scene;
        scene.scene = std::make_unique<rv_editor_scene_doc>();
        add("no scene").app.preset = rv_editor_layout_preset::scene;
    }

    // no project, stopped, running (waiting for a frame), Burn without a candidate, Burn with a candidate.
    void game()
    {
        project_from = 1;
        add("no project", false);
        add("stopped");
        RV_EDITOR_CHECK(session(add("running").app, "run"));
        add("burn, no candidate").app.release_view = true;
        auto &burn = add("burn, candidate").app;
        burn.release_view = true;
        burn.release.candidates.push_back(candidate(999999, false));
    }
};

// What a pane callback saw of its tile body (the "##pane" window).
struct seen {
    rv_editor_shell *shell = nullptr;
    ImVec2 size{ 0, 0 };
    ImVec2 overflow{ 0, 0 }; // drawn content less the room, per axis (negative: room to spare)
};

inline void record_strip(void *context, rv_editor_pane_id id, rv_editor_pane_kind kind, const rv_editor_theme &t)
{
    auto *s = static_cast<seen *>(context);
    rv_editor_shell_pane(s->shell, id, kind, t);
    if (id != strip_pane) {
        return;
    }
    const ImGuiWindow *w = ImGui::GetCurrentWindow();
    s->size = w->Size;
    s->overflow = ImVec2(w->DC.CursorMaxPos.x - w->DC.CursorStartPos.x + w->WindowPadding.x * 2.0f - w->InnerRect.GetWidth(),
        w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y + w->WindowPadding.y * 2.0f - w->InnerRect.GetHeight());
}

// `shell`'s tree becomes the strip pane (pane 1) and a log, drawn the way the app draws its tiles: minimums set, the frame
// start (fits strips and Game), then the workspace. `strip_first`: the strip is the top (or left) child.
struct tiles {
    rv_editor_test_imgui &gui;
    rv_editor_shell &shell;
    seen s;
    rv_editor_probe_record record;

    tiles(rv_editor_test_imgui &g,
        rv_editor_shell &sh,
        rv_editor_pane_kind k,
        bool strip_first,
        rv_editor_tile_dock dock,
        bool untouched)
        : gui(g)
        , shell(sh)
    {
        rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::empty); // pane 0: the strip is pane 1
        const rv_editor_pane_id strip = rv_editor_pane_add(shell.ws.panes, k);
        const rv_editor_pane_id other = rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::output);
        RV_EDITOR_CHECK(strip == strip_pane);
        shell.ws.layout = rv_editor_layout_make(strip_first ? strip : other);
        rv_editor_tile_insert(shell.ws.layout, shell.ws.layout.root, strip_first ? other : strip, dock);
        shell.ws.focused_leaf = shell.ws.layout.root;
        shell.layout_untouched.fill(untouched);
        s.shell = &shell;
    }
    tiles(const tiles &) = delete;
    tiles &operator=(const tiles &) = delete;

    void frame(float width, float height)
    {
        record = rv_editor_probe_draw(
            gui,
            width,
            [&](rv_editor_probe_record &) {
                shell.ws.minimums.clear();
                for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
                    rv_editor_shell_set_minimum(shell,
                        static_cast<rv_editor_pane_id>(i),
                        shell.ws.panes.panes[i].kind,
                        { 0, 0 });
                }
                rv_editor_shell_frame_start(shell);
                std::string error;
                rv_editor_workspace_draw(shell.ws,
                    theme,
                    record_strip,
                    nullptr,
                    &s,
                    { 0, 0, static_cast<int>(width), static_cast<int>(height) },
                    error);
            },
            height);
    }

    // Frames until the layout has settled in a window of this size.
    void settle(float width, float height, int frames = 12)
    {
        for (int i = 0; i < frames; ++i) {
            frame(width, height);
        }
    }

    // The split of the tree (there is only one).
    uint32_t split() const
    {
        for (uint32_t n = 0; n < shell.ws.layout.nodes.size(); ++n) {
            if (shell.ws.layout.nodes[n].kind == rv_editor_tile_kind::split) {
                return n;
            }
        }
        return rv_editor_tile_none;
    }
};

// A shell with an open project in a directory of its own, the Debug preset.
struct own_shell {
    fs::path where = pane_state::fresh_dir("rv_editor_s14_own");
    rv_editor_shell shell;
    own_shell()
    {
        pane_state::open_project(shell.app, where);
        shell.app.preset = rv_editor_layout_preset::debug;
    }
    ~own_shell()
    {
        pane_state::remove_dir(where);
    }
    own_shell(const own_shell &) = delete;
    own_shell &operator=(const own_shell &) = delete;
};

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
    return part.size() == want.size() + 8 && part.compare(0, want.size(), want) == 0;
}

// True when a tile body is a scroll area (rv_editor_scroll_begin): a scroll window directly in a leaf. A converted kind's
// body is a plain "##pane" child instead; the tree's own scroll window ("##scroll" under the test window) is not a leaf's.
inline bool body_is_scroll_area(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        const auto parts = parts_of(e.label);
        if (e.scroll_area && parts.size() >= 2 && is_child(parts.back(), "##scroll") &&
            is_child(parts[parts.size() - 2], "##leaf")) {
            return true;
        }
    }
    return false;
}

// The window of a child named `prefix` in the record, nullptr when there is none.
inline const rv_editor_probe_entry *child_named(const rv_editor_probe_record &r, const char *prefix)
{
    for (const auto &e : r.entries) {
        if (is_child(parts_of(e.label).back(), prefix)) {
            return &e;
        }
    }
    return nullptr;
}

inline bool inside(const rv_editor_probe_entry &e, const rv_editor_probe_entry &box)
{
    return &e != &box && e.x0 >= box.x0 && e.y0 >= box.y0 && e.x1 <= box.x1 && e.y1 <= box.y1;
}

// "" when both records have the same items in the same rectangles, in order, leaving out what lies inside the action slot
// (`##action`) and the message row (`##message`): their windows are compared, what they hold changes with the state.
inline std::string outer_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto outer = [](const rv_editor_probe_record &r) {
        std::vector<rv_editor_probe_entry> out;
        const rv_editor_probe_entry *boxes[] = { child_named(r, "##action"), child_named(r, "##message") };
        for (const auto &e : r.entries) {
            bool held = false;
            for (const auto *box : boxes) {
                held = held || (box != nullptr && inside(e, *box));
            }
            if (!held) {
                out.push_back(e);
            }
        }
        return out;
    };
    const auto x = outer(a);
    const auto y = outer(b);
    if (x.size() != y.size()) {
        return "item count " + std::to_string(x.size()) + " became " + std::to_string(y.size());
    }
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i].x0 != y[i].x0 || x[i].y0 != y[i].y0 || x[i].x1 != y[i].x1 || x[i].y1 != y[i].y1) {
            return "item " + std::to_string(i) + " " + rv_editor_probe_detail::rect(x[i]) + " became " +
                rv_editor_probe_detail::rect(y[i]);
        }
    }
    return "";
}

// The pane of `shell` drawn in a tile of `min` after a settling frame.
inline rv_editor_probe_record
draw_at(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, float w, float h)
{
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_shell_pane(&shell, 1, kind, theme);
    };
    rv_editor_probe_draw(gui, w, body, h);
    return rv_editor_probe_draw(gui, w, body, h);
}

} // namespace s14

} // namespace rv_editor_test
