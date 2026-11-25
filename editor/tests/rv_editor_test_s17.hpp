// Helpers of the S17 tests: the states of Export, Release Candidate, Checks, Observe, Session, Build Result and the empty
// pane as the plan's table lists them, and finders for the areas, the message box and the record rows of a probe record.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_pane_export.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_export_tile.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s3.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s17
{

using namespace rv_editor;
namespace fs = std::filesystem;

inline constexpr rv_editor_pane_id pane = 1;
inline constexpr size_t sha_chars = 64;
inline constexpr size_t path_chars = 100;

inline const std::vector<rv_editor_pane_kind> &kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::export_,
        rv_editor_pane_kind::candidate,
        rv_editor_pane_kind::checks,
        rv_editor_pane_kind::observe,
        rv_editor_pane_kind::session,
        rv_editor_pane_kind::build_result,
        rv_editor_pane_kind::empty };
    return all;
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::export_:
        return "export";
    case rv_editor_pane_kind::candidate:
        return "candidate";
    case rv_editor_pane_kind::checks:
        return "checks";
    case rv_editor_pane_kind::observe:
        return "observe";
    case rv_editor_pane_kind::session:
        return "session";
    case rv_editor_pane_kind::build_result:
        return "build_result";
    default:
        return "empty";
    }
}

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// `chars` characters without a space, so the text can only be shown whole or cut.
inline std::string solid(size_t chars, char c = 'p')
{
    return std::string(chars, c);
}

inline std::string sha256()
{
    return "b4bdd5f80ffbec286eb3836dcdea18be2e042c6fc9a4e5de94e870fac388bfed";
}

inline std::string hex_of(const std::string &text)
{
    static constexpr const char *digits = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : text) {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

inline rv_editor_candidate candidate(uint32_t number)
{
    rv_editor_candidate c;
    c.number = number;
    c.sha256 = sha256();
    c.size = 10365976;
    c.built_at = "2026-01-01 10:00";
    c.checks = rv_editor_checks_make();
    c.image = "/" + solid(path_chars - 1, 'i');
    c.playtests = 1;
    c.last_run_end = "quit";
    c.last_run_clean = true;
    c.burner = "burner 1.0";
    c.baker = "baker 1.0";
    return c;
}

// One state of a pane: a name for the report and what sets it up. Every state sets all it needs, so it can be run after
// any other of the same fixture, except the first state of Session and Build Result that asks for a fresh one.
struct state {
    const char *name;
    std::function<void()> set;
    bool project;   // the state has an open project
    int layout = 0; // states of one layout keep every area and box in the same rectangles; -1: only the boxes are compared
};

struct fixture {
    rv_editor_test_imgui &gui;
    rv_editor_pane_kind kind;
    rv_editor_shell shell;
    fs::path where;
    std::vector<state> states;

    fixture(rv_editor_test_imgui &g, rv_editor_pane_kind k)
        : gui(g)
        , kind(k)
        , where(fs::temp_directory_path() / (std::string("rv_editor_s17_") + name_of(k) + "_" + std::to_string(next_id())))
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        fs::create_directories(where, ec);
        build_states();
    }
    ~fixture()
    {
        end_jobs();
        end_session();
        std::error_code ec;
        fs::remove_all(where, ec);
    }
    fixture(const fixture &) = delete;
    fixture &operator=(const fixture &) = delete;

    // A directory of its own per fixture: the Export form remembers the project it was filled for.
    static int next_id()
    {
        static std::atomic<int> id{ 0 };
        return ++id;
    }

    rv_editor_app &app()
    {
        return shell.app;
    }

    // The state functions of every state, or of the one at `index`.
    std::vector<std::function<void()>> one(size_t index) const
    {
        return { states[index].set };
    }

    void draw(float width, float height)
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
        pane_state::write_script(where / "burner.sh", "exit 0\n");
        export_tile::make_app(app(), where, where / "burner.sh");
    }

    void end_jobs()
    {
        std::string e;
        if (app().exporter.busy()) {
            (void)app().exporter.cancel(e);
        }
        if (app().build.busy()) {
            (void)app().build.cancel(e);
        }
        for (int i = 0; i < 300 && (app().exporter.busy() || app().build.busy()); ++i) {
            (void)app().exporter.update(app().log, e);
            (void)app().build.update(app().log, e);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void end_session()
    {
        std::string e;
        if (app().session.live()) {
            (void)app().session.shutdown(app().log, e);
            pane_state::pump_session(app(), [&] {
                return !app().session.live();
            });
        }
    }

    // No session at all, not even the memory of one that ended: the state does not depend on the state drawn before it.
    void no_session()
    {
        end_session();
        std::destroy_at(&app().session); // neither copyable nor movable: built again where it stands
        std::construct_at(&app().session);
    }

    // No build at all, not even the result of the last one.
    void no_build()
    {
        end_jobs();
        std::destroy_at(&app().build);
        std::construct_at(&app().build);
    }

    // A live session on the stand-in console, connected.
    void live_session(const pane_state::fake_console_options &o = {})
    {
        end_session();
        open_project();
        RV_EDITOR_CHECK(pane_state::start_fake_session(app(), where, o));
    }

    // A live session whose console never answers: the editor is waiting for it.
    void silent_session()
    {
        end_session();
        open_project();
        pane_state::write_script(where / "console.sh", "exec sleep 30\n");
        rv_editor_session_launch launch;
        launch.console = where / "console.sh";
        launch.disc_dir = where / "discdir";
        launch.memcard = where / "card";
        launch.cwd = where;
        std::string e;
        RV_EDITOR_CHECK(app().session.start(launch, app().log, e) == RV_OK);
        RV_EDITOR_CHECK(app().session.live() && !app().session.connected());
    }

    void build_states();
    void export_states();
    void candidate_states();
    void checks_states();
    void observe_states();
    void session_states();
    void build_result_states();
};

// What the states of one fixture drew, to compare with the first of their layout: every message box of every state with
// a project is in the same rectangle, and the areas too in the states of one layout.
struct layouts {
    std::vector<std::pair<int, rv_editor_probe_record>> firsts;
    rv_editor_probe_record first_project;
    bool have_project = false;

    // "" or the first difference between `record` of `st` and the earlier ones.
    std::string add(const state &st, const rv_editor_probe_record &record);
};

// ---- Finders -----------------------------------------------------------------------------------------------------

// The pane's constant minimum, or a roomy fallback when it has none yet, so the checks after the missing minimum still
// say what they find.
inline stable_tiles::minimum tile_of(rv_editor_test_imgui &gui, rv_editor_pane_kind kind)
{
    const auto min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    return min.w > 0.0f && min.h > 0.0f ? min : stable_tiles::minimum{ 420.0f, 360.0f };
}

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

// One state drawn at the pane's minimum on a fixture of its own.
inline rv_editor_probe_record at_minimum(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, size_t state)
{
    fixture f(gui, kind);
    const auto min = tile_of(gui, kind);
    f.states[state].set();
    return settled(f, min.w, min.h);
}

// Like at_minimum, but as tall as a roomy tile: what the content areas hold is counted there, since an item scrolled past an
// area's edge is not in the probe (the minimum only has to fit the frames).
inline constexpr float roomy_h = 900.0f;

inline rv_editor_probe_record at_roomy(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, size_t state)
{
    fixture f(gui, kind);
    const auto min = tile_of(gui, kind);
    f.states[state].set();
    return settled(f, min.w, std::max(min.h, roomy_h));
}

// The frames of the content areas of the record, top to bottom.
inline std::vector<const rv_editor_probe_entry *> areas(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (s13::is_child(s13::parts_of(e.label).back(), "##area")) {
            out.push_back(&e);
        }
    }
    std::sort(out.begin(), out.end(), [](auto *a, auto *b) {
        return a->y0 < b->y0;
    });
    return out;
}

// The text windows of the old message box (and of any 2-line text area with the padding), top to bottom.
inline std::vector<const rv_editor_probe_entry *> texts(const rv_editor_probe_record &r)
{
    return s3::old_boxes(r);
}

inline bool inside(const rv_editor_probe_entry &e, const rv_editor_probe_entry &area)
{
    const float cx = (e.x0 + e.x1) / 2.0f;
    const float cy = (e.y0 + e.y1) / 2.0f;
    return cx >= area.x0 && cx <= area.x1 && cy >= area.y0 && cy <= area.y1;
}

inline bool ends_with(const std::string &s, const char *tail)
{
    const std::string t = tail;
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// The items whose label ends with `tail` (a row's id), in submission order.
inline std::vector<const rv_editor_probe_entry *> labelled(const rv_editor_probe_record &r, const char *tail)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (ends_with(e.label, tail)) {
            out.push_back(&e);
        }
    }
    return out;
}

// The width `text` takes in the font of the pane.
inline float width_of(rv_editor_test_imgui &gui, const std::string &text)
{
    float w = 0.0f;
    gui.frame([&] {
        w = ImGui::CalcTextSize(text.c_str()).x;
    });
    return w;
}

// True when an unlabelled item inside a content area is as wide as `text`: the text is drawn whole there (a cut text
// is one item that is narrower).
inline bool shows_whole(rv_editor_test_imgui &gui, const rv_editor_probe_record &r, const std::string &text)
{
    const float need = width_of(gui, text) - 0.5f;
    for (const auto &e : r.entries) {
        if (e.in_scroll && e.label.empty() && !e.scroll_area && e.x1 - e.x0 >= need) {
            return true;
        }
    }
    return false;
}

inline std::string layouts::add(const state &st, const rv_editor_probe_record &record)
{
    if (!st.project) {
        return "";
    }
    if (!have_project) {
        have_project = true;
        first_project = record;
    } else {
        const auto a = texts(first_project);
        const auto b = texts(record);
        if (a.size() != b.size()) {
            return std::to_string(b.size()) + " text windows where the first state with a project has " +
                std::to_string(a.size());
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (const std::string moved = s13::same_rect(a[i], b[i], "a text window"); !moved.empty()) {
                return moved;
            }
        }
    }
    if (st.layout < 0) {
        return "";
    }
    for (const auto &[layout, first] : firsts) {
        if (layout == st.layout) {
            return s3::windows_difference(first, record);
        }
    }
    firsts.push_back({ st.layout, record });
    return "";
}

// How tall the strip is that the pane draws in the state just set, at the pane's minimum, 0 with none: a strip is taller
// the more it has to say (its text wraps).
inline float strip_height(fixture &f, size_t state)
{
    const auto min = tile_of(f.gui, f.kind);
    f.states[state].set();
    const rv_editor_probe_record r = settled(f, min.w, min.h);
    const auto *strip = s3::strip_of(r);
    return strip != nullptr ? strip->y1 - strip->y0 : 0.0f;
}

// What a window's padding and the spacing between rows leave between two blocks; call inside a frame.
inline float block_gap_limit(rv_editor_test_imgui &gui)
{
    float limit = 0.0f;
    gui.frame([&] {
        limit = 2.0f * ImGui::GetStyle().WindowPadding.y + ImGui::GetStyle().ItemSpacing.y + 2.0f;
    });
    return limit;
}

// "" or the first stretch of the tile's height that no item covers: an empty block (AC21).
inline std::string empty_block(const rv_editor_probe_record &r, float height, float limit)
{
    std::vector<std::pair<float, float>> spans;
    for (const auto &e : r.entries) {
        spans.push_back({ e.y0, e.y1 });
    }
    std::sort(spans.begin(), spans.end());
    float covered = 0.0f;
    for (const auto &[y0, y1] : spans) {
        if (y0 - covered > limit) {
            return "nothing from y=" + std::to_string(covered) + " to y=" + std::to_string(y0);
        }
        covered = std::max(covered, y1);
    }
    return height - covered > limit ? "nothing from y=" + std::to_string(covered) + " to the bottom " + std::to_string(height) :
                                      std::string();
}

} // namespace s17

} // namespace rv_editor_test
