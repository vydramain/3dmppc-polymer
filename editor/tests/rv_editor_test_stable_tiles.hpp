// Helpers of the stable-tile tests: item sizes, probe differences, stand-in builds and the minimum a pane sets.

#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_pane_controls.hpp"
#include "panes/rv_editor_release_facts.hpp"
#include "platform/rv_editor_process.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor_test
{

namespace stable_tiles
{

using namespace rv_editor;

inline constexpr rv_editor_theme theme = rv_editor_theme_olive;

// The size of the item just drawn.
inline ImVec2 item_size()
{
    return ImVec2(ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y);
}

// "" when both draws have the same rectangles in order (ids differ with a label, so they are not matched).
inline std::string same_rects(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    if (a.entries.size() != b.entries.size()) {
        return "item count " + std::to_string(a.entries.size()) + " became " + std::to_string(b.entries.size());
    }
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const auto &x = a.entries[i];
        const auto &y = b.entries[i];
        if (x.x0 != y.x0 || x.y0 != y.y0 || x.x1 != y.x1 || x.y1 != y.y1) {
            return "item " + std::to_string(i) + " " + rv_editor_probe_detail::rect(x) + " became " +
                rv_editor_probe_detail::rect(y);
        }
    }
    return a.content_height == b.content_height ? "" : "content height changed";
}

inline rv_editor_transport_state transport_state(bool resume, bool build_restart)
{
    rv_editor_transport_state s = {};
    s.resume = resume;
    s.build_restart = build_restart;
    return s;
}

// What a fixed button of `label` among `all` occupies.
inline ImVec2 fixed_size(rv_editor_test_imgui &gui, const char *label, std::initializer_list<const char *> all)
{
    ImVec2 size;
    gui.frame([&] {
        rv_editor_button_fixed(label, all, theme);
        size = item_size();
    });
    return size;
}

namespace fs = std::filesystem;

// An open project; build.start needs a root and a cache dir, which `where` gives it.
inline void open_project(rv_editor_app &app, const fs::path &where)
{
    app.project.open = true;
    app.project.root = where;
    app.project.disc_id = "disc";
    app.project.cache_dir = where / "cache";
}

// A burner stand-in that outlives the test's frame; the process is killed with the build.
inline void start_busy_build(rv_editor_app &app, const fs::path &where)
{
    // The process guard starts once per process; another test may have started it, so the result is not checked.
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    const fs::path script = where / "burner.sh";
    std::ofstream(script) << "#!/bin/sh\nsleep 30\n";
    chmod(script.c_str(), 0700);
    app.tools.burner.path = script;
    app.tools.baker.path = script;
    std::string error;
    RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, error) == RV_OK);
    RV_EDITOR_CHECK(app.build.busy());
}

// The height a pane body draws at `width`, from the first cursor row to the last, in the shell's own pane dispatch.
inline float drawn_height(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, float width)
{
    float top = 0.0f;
    const rv_editor_probe_record record = rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
        top = ImGui::GetCursorPosY();
        rv_editor_shell_pane(&shell, 1, kind, theme);
    });
    return record.content_height - top;
}

struct minimum {
    float w, h;
};

// The constant minimum of `kind`, set the way the shell sets it (inside a frame: it measures the font).
inline minimum pane_minimum(rv_editor_test_imgui &gui, rv_editor_pane_kind kind)
{
    minimum result{ 0, 0 };
    gui.frame([&] {
        rv_editor_shell shell;
        rv_editor_shell_set_minimum(shell, 1, kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(1);
        if (it != shell.ws.minimums.end()) {
            result = { static_cast<float>(it->second.w), static_cast<float>(it->second.h) };
        }
    });
    return result;
}

// What the tile draws at its minimum width in one state must fit its constant minimum height, which is the same
// in every state: a minimum under what the tile draws lets the tile clip its content.
inline void check_fits(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, const char *state)
{
    const minimum min = pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.h > 0.0f);
    const float drawn = drawn_height(gui, shell, kind, min.w);
    if (drawn > min.h) {
        std::fprintf(stderr, "  %s: drawn %.1f > minimum %.1f at width %.1f\n", state, drawn, min.h, min.w);
    }
    RV_EDITOR_CHECK(drawn <= min.h);
}

} // namespace stable_tiles

} // namespace rv_editor_test
