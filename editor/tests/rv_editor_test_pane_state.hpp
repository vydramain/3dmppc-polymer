// Helpers for the pane-level tests (rows and stable layout of Candidate, Observe, Session and Build Result): a tall
// headless window, a probe that records the rectangles of what a pane draws, a hash of what was drawn, and a stand-in
// console process that speaks the development protocol so a session can be live or ended without the real console.

#pragma once

#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "app/rv_editor_app.hpp"
#include "imgui_internal.h"
#include "platform/rv_editor_process.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace pane_state
{

namespace fs = std::filesystem;

constexpr float tall = 1400.0f;

// A headless context whose display is tall enough for a whole pane.
struct tall_gui {
    rv_editor_test_imgui gui;
    tall_gui()
    {
        ImGui::GetIO().DisplaySize = ImVec2(800, tall);
        std::string error;
        RV_EDITOR_CHECK(rv_editor::rv_editor_text_load({}, error) == 0);
    }
};

// One frame of `body` in a tall window `width` wide; returns a hash of everything the frame drew.
inline uint64_t frame_hash(tall_gui &g, float width, const std::function<void()> &body)
{
    g.gui.frame(body, width, tall);
    ImGui::Render();
    uint64_t h = 1469598103934665603ull;
    const ImDrawData *data = ImGui::GetDrawData();
    for (int i = 0; data != nullptr && i < data->CmdLists.Size; ++i) {
        const ImDrawList *list = data->CmdLists[i];
        const auto *bytes = reinterpret_cast<const unsigned char *>(list->VtxBuffer.Data);
        const size_t size = static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        for (size_t k = 0; k < size; ++k) {
            h = (h ^ bytes[k]) * 1099511628211ull;
        }
        h = (h ^ static_cast<uint64_t>(list->VtxBuffer.Size)) * 1099511628211ull;
    }
    return h;
}

// What `body` draws in a tall window `width` wide, after `warm` frames so column widths and the like have settled.
inline rv_editor_probe_record settled(tall_gui &g, float width, const std::function<void()> &body, int warm = 20)
{
    rv_editor_probe_record record;
    for (int i = 0; i < warm; ++i) {
        record = {};
        rv_editor_probe_detail::sink sink(&record);
        rv_editor::rv_editor_item_sink_set(&sink);
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        g.gui.frame(
            [&] {
                body();
                record.content_height = ImGui::GetCursorPosY();
            },
            width,
            tall);
        ImGui::GetCurrentContext()->TestEngineHookItems = false;
        rv_editor::rv_editor_item_sink_set(nullptr);
    }
    return record;
}

// "" when every item of one draw is in the other at the same left edge, top and bottom (the right edge follows the
// text a cell holds, so it is not compared) and the content is as high; else the first difference.
inline std::string layout_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    using namespace rv_editor_probe_detail;
    const auto one_way = [](const rv_editor_probe_record &x, const rv_editor_probe_record &y, const char *where) {
        std::vector<bool> used(y.entries.size(), false);
        for (const auto &ex : x.entries) {
            size_t k = 0;
            while (k < y.entries.size() && (used[k] || y.entries[k].id != ex.id)) {
                ++k;
            }
            if (k == y.entries.size()) {
                return name(ex) + " " + rect(ex) + " is missing from the " + where;
            }
            used[k] = true;
            const auto &ey = y.entries[k];
            if (ex.x0 != ey.x0 || ex.y0 != ey.y0 || ex.y1 != ey.y1) {
                return name(ex) + " " + rect(ex) + " became " + rect(ey);
            }
        }
        return std::string();
    };
    std::string d = one_way(a, b, "second draw");
    if (d.empty()) {
        d = one_way(b, a, "first draw");
    }
    if (d.empty() && a.content_height != b.content_height) {
        d = "content height " + std::to_string(a.content_height) + " became " + std::to_string(b.content_height);
    }
    return d;
}

// The Nth item (0-based) labelled `label` (or `label##id`); nullptr when there is none.
inline const rv_editor_probe_entry *nth_item(const rv_editor_probe_record &r, const char *label, int n = 0)
{
    const std::string want = label;
    for (const auto &e : r.entries) {
        if ((e.label == want || e.label.compare(0, want.size() + 2, want + "##") == 0) && n-- == 0) {
            return &e;
        }
    }
    return nullptr;
}

inline const rv_editor_probe_entry *find_item(const rv_editor_probe_record &r, const char *label)
{
    return nth_item(r, label, 0);
}

// "" when no text item (one without a label) reaches past `width`; else the first that does.
inline std::string text_overflow(const rv_editor_probe_record &r, float width)
{
    const ImGuiWindow *implicit = ImGui::FindWindowByName("Debug##Default"); // ImGui's own window is not text
    for (const auto &e : r.entries) {
        if (e.label.empty() && e.x1 > width + 0.5f &&
            (implicit == nullptr || e.x0 != implicit->Pos.x || e.y0 != implicit->Pos.y)) {
            return rv_editor_probe_detail::rect(e) + " reaches past the width " + std::to_string(width);
        }
    }
    return "";
}

// "" when item `label` is in both draws at the very same rectangle; else why not.
inline std::string item_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const char *label)
{
    const auto *ea = find_item(a, label);
    const auto *eb = find_item(b, label);
    if (ea == nullptr || eb == nullptr) {
        return std::string("'") + label + "' is missing from " + (ea == nullptr ? "the first" : "the second") + " draw";
    }
    if (ea->x0 != eb->x0 || ea->y0 != eb->y0 || ea->x1 != eb->x1 || ea->y1 != eb->y1) {
        return std::string("'") + label + "' " + rv_editor_probe_detail::rect(*ea) + " became " +
            rv_editor_probe_detail::rect(*eb);
    }
    return "";
}

// An open project in a directory of its own.
inline void open_project(rv_editor::rv_editor_app &app, const fs::path &where)
{
    app.project.open = true;
    app.project.root = where;
    app.project.disc_id = "disc";
    app.project.cache_dir = where / "cache";
}

inline fs::path fresh_dir(const char *name)
{
    const fs::path where = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(where, ec);
    fs::create_directories(where, ec);
    return where;
}

inline void remove_dir(const fs::path &where)
{
    std::error_code ec;
    fs::remove_all(where, ec);
}

inline void write_script(const fs::path &path, const std::string &body)
{
    std::ofstream(path) << "#!/bin/sh\n" << body;
    chmod(path.c_str(), 0700);
}

// What the stand-in console tells in its status: `with_disc` names a disc, `with_lua` a Lua machine.
struct fake_console_options {
    bool with_disc = true;
    bool with_lua = true;
    std::string disc_hex = "64697363"; // the disc name, hex encoded as the protocol sends it ("disc")
    uint32_t build_number = 1;
    const char *mode = "running"; // "paused": the session reads as paused
    std::string stderr_line;      // the one line the stand-in writes to its stderr at once (none when empty)
    std::string stderr_on_keys;   // the one line it writes to its stderr when asked "keys alpha" (none when empty)
};

// Starts a session of `app` on a stand-in console and waits until it is connected; false when it did not.
// The stand-in answers status, keys (the root: alpha is a table, beta a number; "alpha": one number), get, reload
// and quit.
inline bool start_fake_session(rv_editor::rv_editor_app &app, const fs::path &where, const fake_console_options &o)
{
    std::string guard_error;
    rv_editor::rv_editor_guard_start(guard_error); // once per process: another test may have started it
    const std::string proto = rv_editor::rv_editor_session::protocol_supported;
    const std::string status = "protocol=" + proto + " mode=" + o.mode + " " +
        (o.with_disc ? "disc=" + o.disc_hex + " disc_hash=abc " : "") +
        "pdk=1.0 medium=live entry_reloadable=1 entry_revision=1 entry_hash=h lua_used=10 lua_budget=" +
        (o.with_lua ? "1000" : "0");
    const fs::path script = where / "console.sh";
    write_script(script,
        (o.stderr_line.empty() ? std::string() : "echo '" + o.stderr_line + "' >&2\n") +
            "while read -r id verb rest; do\n"
            "  case \"$verb $rest\" in\n"
            "    \"status \") echo \"$id ok " +
            status +
            "\";;\n"
            "    \"keys \") echo \"$id ok count=3 shown=3 keys=s616c706861:table,s62657461:number,i1:string\";;\n"
            "    \"keys alpha\") " +
            (o.stderr_on_keys.empty() ? std::string() : "echo '" + o.stderr_on_keys + "' >&2; ") +
            "echo \"$id ok count=1 shown=1 keys=s78:number\";;\n"
            "    get*) echo \"$id ok found=1 type=number value=5\";;\n"
            "    \"reload entry\") echo \"$id ok entry_revision=2 entry_hash=h2 lua_used=11\";;\n"
            "    quit*) echo \"$id ok\"; exit 0;;\n"
            "  esac\n"
            "done\n");
    rv_editor::rv_editor_session_launch launch;
    launch.console = script;
    launch.disc_dir = where / "discdir";
    launch.memcard = where / "card";
    launch.cwd = where;
    launch.build_number = o.build_number;
    std::string error;
    if (app.session.start(launch, app.log, error) != RV_OK) {
        return false;
    }
    for (int i = 0; i < 300 && !(app.session.connected() && app.session.facts().revision >= 0); ++i) {
        std::string e;
        app.session.update(app.log, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return app.session.connected();
}

// Runs session updates until `done()` holds or about three seconds pass.
inline bool pump_session(rv_editor::rv_editor_app &app, const std::function<bool()> &done)
{
    for (int i = 0; i < 300 && !done(); ++i) {
        std::string e;
        app.session.update(app.log, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// Asks the stand-in `request` ("keys", "keys alpha") and waits for its answer.
inline bool ask_fake_session(rv_editor::rv_editor_app &app, const std::string &request)
{
    std::string error;
    if (app.session.query(request, app.log, error) != RV_OK) {
        return false;
    }
    return pump_session(app, [&] {
        return app.session.answers().count(request) == 1;
    });
}

// Ends the session by quit and waits until it is over.
inline bool end_fake_session(rv_editor::rv_editor_app &app)
{
    std::string error;
    app.session.stop(app.log, error);
    return pump_session(app, [&] {
        return !app.session.live();
    });
}

} // namespace pane_state

} // namespace rv_editor_test
