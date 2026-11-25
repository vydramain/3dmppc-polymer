// Fake console, candidate images and pane interactions for the playback workflow tests.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

#include "app/rv_editor_shell.hpp"
#include "release/rv_editor_sha256.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor_test
{

namespace workflow_playback
{

using namespace rv_editor;

constexpr float pane_width = 1160.0f;
constexpr float pane_height = 360.0f;

struct playback_fixture {
    std::filesystem::path root;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app &app = shell.app;

    explicit playback_fixture(bool with_fonts = false)
        : gui(with_fonts)
    {
        char scratch[] = "rv-editor-playback-XXXXXX";
        const char *created = mkdtemp(scratch);
        RV_EDITOR_CHECK(created != nullptr);
        if (created == nullptr) {
            return;
        }
        root = std::filesystem::absolute(created);
        stable_tiles::open_project(app, root);
        app.project.state_dir = root / "state";
        std::filesystem::create_directories(app.project.cache_dir / "candidates");

        std::string error;
        rv_editor_guard_start(error);
        app.tools.burner.path = root / "burner";
        app.tools.baker.path = app.tools.burner.path;
        pane_state::write_script(app.tools.burner.path, "mkdir -p \"$4\"\n");
        app.tools.console.path = root / "console";
        pane_state::write_script(app.tools.console.path,
            "for disc do :; done\n"
            "case \"$disc\" in *.mppcdisc) medium=fixed;; *) medium=live;; esac\n"
            "echo \"playback started: $disc\" >&2\n"
            "while read -r id verb rest; do\n"
            "  case \"$verb\" in\n"
            "    status) echo \"$id ok protocol=" +
                std::string(rv_editor_session::protocol_supported) +
                " mode=running disc=64697363 pdk=1.0 medium=$medium entry_revision=1\";;\n"
                "    quit) echo \"$id ok\"; exit 0;;\n"
                "  esac\n"
                "done\n");
        rv_editor_shell_load_layouts(shell, {}, "code");
        rv_editor_shell_update(shell);
    }

    ~playback_fixture()
    {
        if (app.session.live()) {
            pane_state::end_fake_session(app);
        }
        if (!root.empty()) {
            pane_state::remove_dir(root);
        }
    }

    void add_candidate(uint32_t number, const std::string &bytes)
    {
        rv_editor_candidate candidate;
        candidate.number = number;
        candidate.image = app.project.cache_dir / "candidates" / (std::to_string(number) + ".mppcdisc");
        std::ofstream(candidate.image) << bytes;
        candidate.checks = rv_editor_checks_make();
        std::string error;
        candidate.sha256 = rv_editor_sha256_file(candidate.image, candidate.size, error);
        RV_EDITOR_CHECK(!candidate.sha256.empty());
        app.release.candidates.push_back(std::move(candidate));
    }

    void switch_to(rv_editor_layout_preset preset)
    {
        rv_editor_shell_switch(shell, preset);
        rv_editor_shell_update(shell);
    }

    template <class Ready>
    bool wait_until(Ready ready)
    {
        for (int i = 0; i < 300; ++i) {
            rv_editor_shell_update(shell);
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    rv_editor_probe_record draw(rv_editor_pane_kind pane)
    {
        return rv_editor_probe_draw(
            gui,
            pane_width,
            [&](rv_editor_probe_record &) {
                shell.ui_requests.clear();
                rv_editor_shell_pane(&shell, 1, pane, stable_tiles::theme);
            },
            pane_height);
    }

    bool click_run(rv_editor_pane_kind pane, const char *key)
    {
        draw(pane);
        const auto shown = draw(pane);
        const auto button = std::find_if(shown.entries.begin(), shown.entries.end(), [&](const auto &entry) {
            return entry.label == rv_editor_text(key);
        });
        if (button == shown.entries.end()) {
            return false;
        }
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((button->x0 + button->x1) / 2.0f, (button->y0 + button->y1) / 2.0f);
        draw(pane);
        io.AddMouseButtonEvent(0, true);
        draw(pane);
        io.AddMouseButtonEvent(0, false);
        draw(pane);
        return wait_until([&] {
            return app.session.connected();
        });
    }

    bool stop()
    {
        std::string error;
        return app.session.stop(app.log, error) == RV_OK && wait_until([&] {
            return !app.session.live();
        });
    }
};

} // namespace workflow_playback

} // namespace rv_editor_test
