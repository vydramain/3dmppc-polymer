// The fixture of the S15 tests: the states of Project, Files, Assets, Run Config, Findings and Test Case as the plan lists
// them.

#pragma once

#include "rv_editor_test_s15_find.hpp"

namespace rv_editor_test
{

namespace s15
{

// The sizes the content tests draw at: the kind's minimum and a roomy one. A kind with no minimum yet is drawn at the
// roomy size only, so the checks fail on what the pane lacks and not on a window of no size.
inline std::vector<ImVec2> tile_sizes(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, ImVec2 roomy)
{
    const auto min = stable_tiles::pane_minimum(gui, kind);
    if (min.w <= 0.0f || min.h <= 0.0f) {
        return { roomy };
    }
    return { ImVec2(min.w, min.h), roomy };
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
        , where(fs::temp_directory_path() / (std::string("rv_editor_s15_") + name_of(k) + "_" + std::to_string(getpid())))
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        make_tree();
        build_states();
    }
    ~fixture()
    {
        std::string error;
        (void)shell.app.files.close(error);
        std::error_code ec;
        fs::remove_all(where, ec);
    }
    fixture(const fixture &) = delete;
    fixture &operator=(const fixture &) = delete;

    rv_editor_app &app()
    {
        return shell.app;
    }
    rv_editor_files_view &view()
    {
        return app().files_views[pane];
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

    // Two settling frames, then the one recorded.
    rv_editor_probe_record settled(float width, float height)
    {
        rv_editor_probe_record record;
        for (int i = 0; i < 3; ++i) {
            record = rv_editor_probe_draw(
                gui,
                width,
                [&](rv_editor_probe_record &) {
                    rv_editor_shell_pane(&shell, pane, kind, stable_tiles::theme);
                },
                height);
        }
        return record;
    }

    // A press and a release at the middle of `e`, then the pointer away.
    void click(const rv_editor_probe_entry &e, float width, float height, int button = 0)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
        draw(width, height);
        io.AddMouseButtonEvent(button, true);
        draw(width, height);
        io.AddMouseButtonEvent(button, false);
        draw(width, height);
        io.AddMousePosEvent(-1000.0f, -1000.0f);
        draw(width, height);
    }

    void make_tree()
    {
        fs::create_directories(where / "sub");
        std::ofstream(where / "a.txt").put('x');
        std::ofstream(where / "sub" / "inner.txt").put('x');
        std::ofstream(where / (words(120) + ".txt")).put('x');
        for (int i = 0; i < files_count; ++i) {
            std::ofstream(where / ("f" + std::to_string(100 + i) + ".txt")).put('x');
        }
        // Assets looks at the folders of a project root: one with resources, one without.
        fs::create_directories(where / "empty");
        for (const char *dir : { "gfx", "sfx", "models", "notes" }) {
            fs::create_directories(where / "full" / dir);
        }
        std::ofstream(where / "full" / "gfx" / "hero.png") << "not a png";
        std::ofstream(where / "full" / "sfx" / "beep.wav") << "not a wav";
        std::ofstream(where / "full" / "models" / "ship.glb") << "not a glb";
        std::ofstream(where / "full" / "notes" / "readme.txt") << "words";
        std::ofstream(where / "full" / "notes" / (words(100) + ".txt")) << "words";
    }

    void open_project(const fs::path &root)
    {
        app().project.open = true;
        app().project.root = root;
        app().project.disc_id = "disc";
        app().project.cache_dir = where / "cache";
        app().project.state_dir = where / ".state";
    }

    void no_project()
    {
        app().project.open = false;
    }

    void open_files()
    {
        open_project(where);
        std::string error;
        if (!app().files.is_open()) {
            RV_EDITOR_CHECK(app().files.open(where, error) == 0);
        }
        shell.ui.files_selected = where / "a.txt";
    }

    void ask(rv_editor_files_view::rv_editor_files_dialog dialog,
        const fs::path &target,
        const std::string &name,
        const std::string &error = {})
    {
        open_files();
        auto &v = view();
        v = {};
        v.dialog = dialog;
        v.target = target;
        std::snprintf(v.name, sizeof(v.name), "%s", name.c_str());
        v.error = error;
    }

    void build_states()
    {
        switch (kind) {
        case rv_editor_pane_kind::project:
            project_states();
            break;
        case rv_editor_pane_kind::files:
            files_states();
            break;
        case rv_editor_pane_kind::assets:
            assets_states();
            break;
        case rv_editor_pane_kind::run_config:
            run_config_states();
            break;
        case rv_editor_pane_kind::findings:
            findings_states();
            break;
        default:
            test_case_states();
            break;
        }
    }

    // --- Project: no project, open, manifest error, tool missing, settings error, build running.
    void project_reset()
    {
        open_project(where);
        auto &t = app().tools;
        for (rv_editor_tool *tool :
            { &t.console, &t.burner, &t.firmware, &t.texbaker, &t.musbaker, &t.modbaker, &t.inspector }) {
            tool->path = where / "bin" / words(60);
            tool->origin = "next to the editor";
            tool->problem.clear();
            tool->version = "version 1.2.3 " + words(100);
        }
        t.settings_path = where / ".state" / "settings.toml";
        t.settings_error.clear();
        app().project.manifest_error.clear();
    }

    void project_states()
    {
        states = {
            [this] {
                project_reset();
                no_project();
            },
            [this] {
                project_reset();
            },
            [this] {
                project_reset();
                app().project.manifest_error = "disc.toml:3: " + words(300);
            },
            [this] {
                project_reset();
                app().tools.burner.problem = "cannot run the burner: " + words(300);
            },
            [this] {
                project_reset();
                app().tools.settings_error = "settings.toml:2: " + words(300);
            },
            [this] {
                project_reset();
                if (!app().build.busy()) {
                    stable_tiles::start_busy_build(app(), where);
                }
            },
        };
    }

    // --- Files: no project, tree, new file, rename, delete file, delete folder, error.
    void files_states()
    {
        using dialog = rv_editor_files_view::rv_editor_files_dialog;
        states = {
            [this] {
                view() = {};
                std::string error;
                (void)app().files.close(error);
                no_project();
            },
            [this] {
                ask(dialog::none, {}, "");
            },
            [this] {
                ask(dialog::new_file, where, "");
            },
            [this] {
                ask(dialog::rename, where / "a.txt", "a.txt");
            },
            [this] {
                ask(dialog::remove, where / "a.txt", "");
                view().doomed = { "a.txt" };
                view().doomed_total = 1;
            },
            [this] {
                ask(dialog::remove, where / "sub", "");
                for (int i = 0; i < 12; ++i) {
                    view().doomed.push_back("sub/Q" + std::to_string(i) + "_" + words(60) + ".txt");
                }
                view().doomed_total = 214;
            },
            [this] {
                ask(dialog::rename, where / "a.txt", "a.txt", "a file of that name exists: " + words(300));
            },
        };
    }

    // The resources of `where/full` as the cells of the grid, in the order Assets sorts them: gfx/hero.png,
    // models/ship.glb, notes/readme.txt, notes/<long>.txt, sfx/beep.wav.
    void select_asset(size_t index, float width, float height)
    {
        app().assets_ui.details = false;
        const auto record = settled(width, height);
        size_t seen = 0;
        for (const auto &e : record.entries) {
            if (e.label == "##cell" && seen++ == index) {
                click(e, width, height);
                break;
            }
        }
    }

    // --- Assets: no project, empty, filter without matches, grid, table, then the preview of a picture, a sound, a
    // model and a text.
    void assets_states()
    {
        const auto in_full = [this] {
            open_project(where / "full");
            app().assets_ui.filter[0] = '\0';
            app().assets_ui.folder = 0;
        };
        states = {
            [this] {
                open_project(where / "empty");
                no_project();
            },
            [this] {
                open_project(where / "empty");
                app().assets_ui.filter[0] = '\0';
            },
            [this] {
                open_project(where / "full");
                app().assets_ui.folder = 0;
                std::snprintf(app().assets_ui.filter, sizeof(app().assets_ui.filter), "%s", "matches nothing at all");
            },
            [this, in_full] {
                in_full();
                app().assets_ui.details = false;
            },
            [this, in_full] {
                in_full();
                app().assets_ui.details = true;
            },
            [this, in_full] {
                in_full();
                select_asset(0, 600.0f, 500.0f);
            },
            [this, in_full] {
                in_full();
                select_asset(4, 600.0f, 500.0f);
            },
            [this, in_full] {
                in_full();
                select_asset(1, 600.0f, 500.0f);
            },
            [this, in_full] {
                in_full();
                select_asset(3, 600.0f, 500.0f);
            },
        };
    }

    // --- Run Config: no project, clean, long values, profile error, problem of the profile.
    void run_profiles(bool long_values)
    {
        open_project(where);
        app().run_config = {};
        auto &profile = app().run_config.profiles[0];
        profile.runtime = long_values ? (where / "src" / "deep" / std::string(60, 'Z')).string() : "";
        profile.cwd = long_values ? (where / "src" / "deep" / std::string(60, 'J')).string() : "";
        profile.memcard = long_values ? (where / "src" / "deep" / std::string(60, 'X')).string() : "";
        auto second = profile;
        second.name = "second-profile-" + words(60);
        app().run_config.profiles.push_back(second);
        second.name = "third";
        app().run_config.profiles.push_back(second);
        app().run_config.error.clear();
        app().run_config_revision = ++revision;
    }

    void run_config_states()
    {
        states = {
            [this] {
                run_profiles(false);
                no_project();
            },
            [this] {
                run_profiles(false);
            },
            [this] {
                run_profiles(true);
            },
            [this] {
                run_profiles(false);
                app().run_config.error = "project.toml: " + words(300);
            },
            [this] {
                run_profiles(false);
                app().run_config.profiles[0].runtime = "/nonexistent/" + std::string(80, 'r');
            },
        };
    }

    // --- Findings: no project, empty, error, long text and a capture, saved.
    void findings_states()
    {
        const auto reset = [this] {
            open_project(where);
            auto &f = app().findings;
            f = {};
        };
        states = {
            [this, reset] {
                reset();
                no_project();
            },
            [this, reset] {
                reset();
            },
            [this, reset] {
                reset();
                app().findings.error = "cannot write: " + words(300);
            },
            [this, reset] {
                reset();
                std::snprintf(app().findings.title, sizeof(app().findings.title), "%s", words(120).c_str());
                std::snprintf(app().findings.steps, sizeof(app().findings.steps), "%s", words(300).c_str());
                app().findings.capture = where / (words(100) + ".png");
            },
            [this, reset] {
                reset();
                for (int i = 0; i < 6; ++i) {
                    app().findings.saved.push_back(where / "findings" / (std::to_string(i) + "_" + words(100) + ".md"));
                }
            },
        };
    }

    // --- Test Case: no project, exploratory, a case picked, a long note, results, error.
    void test_case_states()
    {
        const auto reset = [this](int32_t picked) {
            open_project(where);
            auto &f = app().findings;
            f = {};
            rv_editor_listed_case c;
            c.path = where / "testcases" / "case-1.txt";
            c.title = "Title " + words(200);
            c.steps = "Step one. " + words(300);
            c.expected = "Expect one. " + words(300);
            f.cases = { c, c };
            f.test_case = picked;
        };
        states = {
            [this, reset] {
                reset(-1);
                no_project();
            },
            [this, reset] {
                reset(-1);
            },
            [this, reset] {
                reset(0);
            },
            [this, reset] {
                reset(0);
                std::snprintf(app().findings.note, sizeof(app().findings.note), "%s", words(200).c_str());
            },
            [this, reset] {
                reset(0);
                app().findings.results = { "Passed: a " + words(100), "Failed: b", "Passed: c", "Blocked: d" };
            },
            [this, reset] {
                reset(0);
                app().findings.error = "cannot write: " + words(300);
            },
        };
    }
};

} // namespace s15

} // namespace rv_editor_test
