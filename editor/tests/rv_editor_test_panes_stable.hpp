// Helpers of the stable-pane tests: probe pairs, label and field collisions, and a stand-in project for Run Config.

#pragma once

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

namespace rv_editor_test
{

namespace panes_stable
{

using namespace rv_editor;

namespace fs = std::filesystem;

inline constexpr rv_editor_theme theme = rv_editor_theme_olive;
inline constexpr float width = 400.0f;
inline constexpr float tall = 900.0f; // tall enough for a whole pane: the test window never clips or reorders it

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// A fresh directory for one test, removed by the caller.
inline fs::path make_dir(const char *name)
{
    const fs::path dir =
        fs::temp_directory_path() / (std::string("rv_editor_test_s13_") + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

inline std::string long_words()
{
    std::string s;
    while (s.size() < 200) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

inline const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

inline const rv_editor_probe_entry *find_part(const rv_editor_probe_record &r, const std::string &part)
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
inline std::string
stable(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const std::vector<std::string> &anchors)
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
inline std::string
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
inline std::string collisions(const rv_editor_probe_record &r, float window_width)
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

inline uint64_t revision = 5000; // the pane refills its form when this moves; one per draw keeps tests apart

inline void run_open(rv_editor_app &app, const fs::path &root)
{
    app.project.open = true;
    app.project.root = root;
    app.run_config = {};
    app.run_config_revision = ++revision;
}

inline rv_editor_probe_record run_draw(rv_editor_test_imgui &gui, rv_editor_app &app, float w)
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

inline void run_set_paths(rv_editor_app &app, const path_world &w)
{
    app.run_config.profiles[0].runtime = w.runtime;
    app.run_config.profiles[0].cwd = w.cwd;
    app.run_config.profiles[0].memcard = w.memcard;
}

// Every path field, unfocused, shows the end of its path ('Z', 'J', 'X'); read from the field's probe. Outside the root
// (`root` empty) the shown text is "..." + the end of the path, shorter than the path; inside it the path is relative.
inline void check_tails(rv_editor_test_imgui &gui, rv_editor_app &app, const std::string &root = {})
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

} // namespace panes_stable

} // namespace rv_editor_test
