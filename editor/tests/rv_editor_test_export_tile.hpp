// Helpers of the Export tile tests: a stand-in burner, an app over it, a probe of the places a tile draws.

#pragma once

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

namespace rv_editor_test
{

namespace export_tile
{

using namespace rv_editor;

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

inline rv_editor_progress progress_with(rv_editor_step_state a, rv_editor_step_state b, rv_editor_run_end end)
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

inline constexpr rv_editor_theme theme = rv_editor_theme_olive;
inline constexpr float wide = 420.0f;
inline constexpr float narrow = 230.0f;
inline constexpr const char *result_buttons[] = { "Open Folder", "Open in Browser", "Show in Problems" };
inline constexpr const char *action_buttons[] = { "Export", "Cancel" };

inline fs::path scratch(const char *name)
{
    const fs::path dir =
        fs::temp_directory_path() / (std::string("rv_editor_test_tile_") + name + "_" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// A burner stand-in running `body`; the export's output directory is $6.
inline fs::path script(const fs::path &where, const char *body)
{
    const fs::path file = where / "burner.sh";
    std::ofstream(file) << "#!/bin/sh\n" << body << "\n";
    chmod(file.c_str(), 0700);
    return file;
}

// An app with a stand-in project and burner.
inline void make_app(rv_editor_app &app, const fs::path &root, const fs::path &burner)
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

inline void settle(rv_editor_app &app, rv_editor_build_state want)
{
    for (int i = 0; i < 500 && app.exporter.state() != want; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string error;
        app.exporter.update(app.log, error);
    }
}

inline bool contains(const std::string &s, const char *part)
{
    return s.find(part) != std::string::npos;
}

// The entries that name a block, the kind control or a button (the contents of a block differ by state; its place may not).
inline bool placed(const rv_editor_probe_entry &e)
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

inline rv_editor_probe_record placed_only(const rv_editor_probe_record &r)
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

inline rv_editor_probe_record draw_tile(rv_editor_test_imgui &gui, rv_editor_app &app, float width)
{
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_pane_export(app, theme);
    };
    // Tall enough for the whole tile, so the result of the bottom block is inside the window clip rect.
    constexpr float height = 800.0f;
    rv_editor_probe_draw(gui, width, body, height); // an auto-sized child settles on the second frame
    return placed_only(rv_editor_probe_draw(gui, width, body, height));
}

inline const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const char *part)
{
    for (const auto &e : r.entries) {
        if (contains(e.label, part)) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace export_tile

} // namespace rv_editor_test
