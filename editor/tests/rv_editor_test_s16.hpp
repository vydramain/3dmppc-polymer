// Helpers of the S16 tests: the states of Scene, Hierarchy and Inspector as the plan's table lists them, each set so that
// running it again after the others gives the same screen. The finders of S13 (message box, content area, windows
// difference) are reused.

#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "pdkkit/component/rv_component_record.hpp"
#include "project/rv_editor_descriptions.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace s16
{

using namespace rv_editor;
namespace fs = std::filesystem;
using s13::content_area;
using s13::has_ask_place;
using s13::load_texts;
using s13::message_box;
using s13::words;

inline constexpr rv_editor_pane_id pane = 1;
inline constexpr const char *comp_name = "probe_comp";
inline constexpr const char *long_label = "a_field_with_a_very_long_name_";

inline const std::vector<rv_editor_pane_kind> &kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::scene,
        rv_editor_pane_kind::hierarchy,
        rv_editor_pane_kind::inspector };
    return all;
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    return kind == rv_editor_pane_kind::scene ? "scene" : kind == rv_editor_pane_kind::hierarchy ? "hierarchy" : "inspector";
}

// A shell with the states of one pane kind (pane 1), in the order of the plan's table. The New Scene question is state
// of the process (the shell's global), so every state closes it first and the destructor leaves it closed.
struct fx {
    rv_editor_test_imgui &gui;
    rv_editor_pane_kind kind;
    rv_editor_shell shell;
    fs::path where;
    std::vector<std::function<void()>> states;

    fx(rv_editor_test_imgui &g, rv_editor_pane_kind k)
        : gui(g)
        , kind(k)
        , where(fs::temp_directory_path() / (std::string("rv_editor_s16_") + name_of(k)))
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        fs::create_directories(where / "scenes", ec);
        // Scenes the no-scene states offer to open; with them a list is longer than one row.
        for (const char *scene : { "first_level", "second_level", "third_level" }) {
            std::ofstream(where / "scenes" / (std::string(scene) + ".scene.toml")) << "\n";
        }
        build_states();
    }
    ~fx()
    {
        if (kind == rv_editor_pane_kind::scene) {
            base();
            close_ask();
        }
        std::error_code ec;
        fs::remove_all(where, ec);
    }
    fx(const fx &) = delete;
    fx &operator=(const fx &) = delete;

    rv_editor_app &app()
    {
        return shell.app;
    }
    rv_editor_scene_object &object()
    {
        return app().scene->scene.objects[1];
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

    // Escape closes the New Scene question while its pane has the keyboard (needs a project and a scene tab in front).
    void close_ask()
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        draw();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        draw();
    }

    // Project open on a pdkkit disc, a scene of two objects with a described component on the second, nothing said.
    void base()
    {
        stable_tiles::open_project(app(), where);
        app().project.sdk = rv_editor_sdk_state::pdkkit;
        app().scene = std::make_unique<rv_editor_scene_doc>();
        app().scene->scene = rv_editor_scene_make(where / "scenes" / "a.scene.toml");
        app().scene->selected.clear();
        app().scene_error.clear();
        app().scene_move_note.clear();
        shell.ui.scene_tabs = {};
        rv_pdkkit::ecs_component_desc desc{};
        std::strcpy(desc.name, comp_name);
        desc.field_count = 1;
        std::strcpy(desc.fields[0].name, long_label);
        desc.fields[0].type = rv_pdkkit::ecs_field_type::string;
        std::strcpy(desc.fields[0].def.text, "dflt");
        app().descriptions.snapshot.state = rv_editor_descriptions_state::loaded;
        app().descriptions.snapshot.records.clear();
        app().descriptions.snapshot.records.push_back(rv_pdkkit::ecs_record_make(desc));
        rv_editor_scene_component c;
        c.name = comp_name;
        object().components.push_back(c);
        if (kind == rv_editor_pane_kind::scene) {
            close_ask();
        }
    }

    void build_states()
    {
        if (kind == rv_editor_pane_kind::scene) {
            scene_states();
        } else if (kind == rv_editor_pane_kind::hierarchy) {
            hierarchy_states();
        } else {
            inspector_states();
        }
    }

    // no project, no scene, scene, read-only, error, New Scene question, three tabs, twelve tabs.
    void scene_states()
    {
        const auto tabs = [this](size_t n) {
            for (size_t i = 0; i < n; ++i) {
                shell.ui.scene_tabs.tabs.push_back(
                    { where / ("picture_" + std::to_string(i) + ".png"), rv_editor_scene_tab_kind::picture, 0.0 });
            }
        };
        states = {
            [this] {
                base();
                app().project.open = false;
                app().scene.reset();
            },
            [this] {
                base();
                app().scene.reset();
            },
            [this] {
                base();
            },
            [this] {
                base();
                app().scene->scene.read_only = words(300);
            },
            [this] {
                base();
                app().scene_error = "scenes/b.scene.toml: " + words(300);
            },
            [this] {
                base();
                rv_editor_shell_new_scene_request(app());
            },
            [this, tabs] {
                base();
                tabs(3);
            },
            [this, tabs] {
                base();
                tabs(12);
            },
        };
    }

    // no scene, tree, filter with no match, read-only, move note, selection.
    void hierarchy_states()
    {
        states = {
            [this] {
                base();
                app().scene.reset();
            },
            [this] {
                base();
            },
            [this] {
                base();
                std::snprintf(app().scene->hierarchy_query,
                    sizeof(app().scene->hierarchy_query),
                    "%s",
                    "matches no object at all");
            },
            [this] {
                base();
                app().scene->scene.read_only = words(300);
            },
            [this] {
                base();
                app().scene_move_note = "Not moved: " + words(300);
            },
            [this] {
                base();
                app().scene->selected = object().id;
            },
        };
    }

    // no scene, nothing selected, object, read-only, refusal.
    void inspector_states()
    {
        states = {
            [this] {
                base();
                app().scene.reset();
            },
            [this] {
                base();
            },
            [this] {
                base();
                app().scene->selected = object().id;
            },
            [this] {
                base();
                app().scene->selected = object().id;
                app().scene->scene.read_only = words(300);
            },
            [this] {
                base();
                app().scene->selected = object().id;
                rv_editor_scene_doc &doc = *app().scene;
                doc.field_refusal = { object().id, comp_name, long_label, words(300), doc.steps, doc.undo.size() };
            },
        };
    }
};

// Draws the pane of `f` after a few settling frames at `width` x `height` and returns what it drew.
inline rv_editor_probe_record settled(fx &f, float width, float height)
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

inline const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const char *label)
{
    for (const auto &e : r.entries) {
        if (e.label == label || e.label.compare(0, std::strlen(label) + 2, std::string(label) + "##") == 0) {
            return &e;
        }
    }
    return nullptr;
}

inline void report(const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s: %s\n", what, bad.c_str());
    }
}

} // namespace s16

} // namespace rv_editor_test
