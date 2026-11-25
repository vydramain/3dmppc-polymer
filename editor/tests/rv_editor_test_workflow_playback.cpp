// AC11: a running project build or candidate keeps its identity across layouts, and playtest checks refer to the image run.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "release/rv_editor_candidate_store.hpp"
#include "release/rv_editor_sha256.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "rv_editor_test_workflow_playback_fixture.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using rv_editor_test::workflow_playback::playback_fixture;

namespace
{

// Match the preview's drawn glyphs. Its overlay is rendered directly to the draw list.
bool preview_shows(playback_fixture &fixture, std::string_view text)
{
    fixture.draw(rv_editor_pane_kind::game);
    fixture.draw(rv_editor_pane_kind::game);
    bool found = false;
    fixture.gui.frame([&] {
        std::vector<ImVec2> glyphs;
        for (const char letter : text) {
            if (letter != ' ') {
                const ImFontGlyph *glyph = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(letter));
                glyphs.emplace_back(glyph->U0, glyph->V0);
            }
        }
        for (const ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
            if (!window->WasActive || std::string_view(window->Name).find("##well") == std::string_view::npos) {
                continue;
            }
            const auto &vertices = window->DrawList->VtxBuffer;
            const int span = 4 * static_cast<int>(glyphs.size());
            for (int i = 0; i + span <= vertices.Size; ++i) {
                int j = i;
                if (std::all_of(glyphs.begin(), glyphs.end(), [&](ImVec2 uv) {
                        const ImVec2 drawn = vertices[j].uv;
                        j += 4;
                        return drawn.x == uv.x && drawn.y == uv.y;
                    })) {
                    found = true;
                    return;
                }
            }
        }
    });
    return found;
}

std::string controls_status(playback_fixture &fixture, rv_editor_pane_kind pane)
{
    fixture.draw(pane);
    std::string shown = rv_editor_strip_status_last();
    std::transform(shown.begin(), shown.end(), shown.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return shown;
}

bool shows_session(std::string_view status, uint32_t number)
{
    const std::string id = std::to_string(number);
    return status.find("session " + id) != std::string::npos || status.find("session #" + id) != std::string::npos;
}

// Read only the session's process so the app has not yet collected the asynchronous launch hash.
bool end_with_hash_pending(playback_fixture &fixture)
{
    auto &app = fixture.app;
    auto &candidate = app.release.candidates.front();
    if (rv_editor_app_run_candidate(app) != RV_OK ||
        !pane_state::pump_session(app,
            [&] {
                return app.session.connected();
            }) ||
        !pane_state::end_fake_session(app)) {
        return false;
    }
    return app.session.state() == rv_editor_run_state::exited && candidate.hashing.valid();
}

void check_run_or_refusal(rv_editor_app &app, int result, uint32_t previous, const char *why, uint64_t before)
{
    if (result == RV_OK) {
        RV_EDITOR_CHECK(app.session.number() != previous);
        return;
    }
    RV_EDITOR_CHECK(result == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.session.number() == previous);
    RV_EDITOR_CHECK(
        (why != nullptr && *why != '\0') || std::any_of(app.log.lines().begin(), app.log.lines().end(), [&](const auto &line) {
            return line.seq > before && line.level == rv_editor_log_level::warning && !line.text.empty();
        }));
}

void check_finished_playtest(playback_fixture &fixture)
{
    const auto &original = fixture.app.release.candidates.front();
    RV_EDITOR_CHECK(original.playtests == 1);
    RV_EDITOR_CHECK(original.last_run_clean);
    const auto &loads = original.checks[rv_editor_check_loads];
    RV_EDITOR_CHECK(loads.state == rv_editor_check_state::passed && rv_editor_check_valid(original, loads));
    const auto path = rv_editor_candidate_log(original.image.parent_path(), original.number, "playtest-1");
    std::ifstream file(path);
    const std::string log{ std::istreambuf_iterator<char>(file), {} };
    const auto start = log.find("playback started: " + original.image.string());
    RV_EDITOR_CHECK(start != std::string::npos);
    RV_EDITOR_CHECK(log.find("ended: stopped", start) != std::string::npos);
    RV_EDITOR_CHECK(log.find("run profile") == std::string::npos);
    if (start != std::string::npos) {
        RV_EDITOR_CHECK(log.find("playback started:", start + 1) == std::string::npos);
    }
}

// Search the rendered text inside the tile and its child windows. Glyphs in the tooltip or outside a window's clip
// rectangle do not count. Spaces are skipped because wrapping the status can consume them between lines.
bool controls_show(playback_fixture &fixture, rv_editor_pane_kind pane, stable_tiles::minimum min, std::string_view text)
{
    bool found = false;
    fixture.gui.frame(
        [&] {
            rv_editor_shell_pane(&fixture.shell, 1, pane, stable_tiles::theme);
        },
        min.w,
        min.h);
    fixture.gui.frame(
        [&] {
            ImGuiWindow *tile = ImGui::GetCurrentWindow();
            rv_editor_shell_pane(&fixture.shell, 1, pane, stable_tiles::theme);
            std::vector<ImVec2> glyphs;
            for (const char letter : text) {
                if (letter != ' ') {
                    const ImFontGlyph *glyph = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(letter));
                    glyphs.emplace_back(glyph->U0, glyph->V0);
                }
            }
            for (const ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
                if (!window->Active || window->RootWindow != tile) {
                    continue;
                }
                ImRect clip = window->InnerClipRect;
                clip.ClipWith(ImRect(0, 0, min.w, min.h));
                const auto &vertices = window->DrawList->VtxBuffer;
                const int span = 4 * static_cast<int>(glyphs.size());
                for (int i = 0; i + span <= vertices.Size; ++i) {
                    int j = i;
                    if (std::all_of(glyphs.begin(), glyphs.end(), [&](ImVec2 uv) {
                            const ImDrawVert &first = vertices[j];
                            const ImDrawVert &last = vertices[j + 2];
                            j += 4;
                            return first.uv.x == uv.x && first.uv.y == uv.y && clip.Contains(first.pos) &&
                                clip.Contains(last.pos);
                        })) {
                        found = true;
                        return;
                    }
                }
            }
        },
        min.w,
        min.h);
    return found;
}

} // namespace

RV_EDITOR_TEST(editor_playback_project_keeps_its_build_and_session_in_burn)
{
    playback_fixture fixture;
    fixture.add_candidate(1, "candidate with the same number as the project build");
    const bool started = fixture.click_run(rv_editor_pane_kind::game, "pane_game.run");
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }
    auto &app = fixture.app;
    const auto number = app.session.number();
    const auto disc = app.session.disc_dir();
    RV_EDITOR_CHECK(app.session.facts().medium == "live");
    RV_EDITOR_CHECK(app.build.last_success().has_value() && disc == app.build.last_success()->dir);
    RV_EDITOR_CHECK(disc != app.release.candidates[0].image);

    fixture.switch_to(rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(app.session.number() == number && app.session.connected());
    RV_EDITOR_CHECK(app.session.disc_dir() == disc);
    RV_EDITOR_CHECK(preview_shows(fixture, "Build #1") || preview_shows(fixture, "build #1"));
    const std::string burn_status = controls_status(fixture, rv_editor_pane_kind::release_controls);
    RV_EDITOR_CHECK(burn_status.find("build #1") != std::string::npos);
    RV_EDITOR_CHECK(shows_session(burn_status, number));

    fixture.switch_to(rv_editor_layout_preset::code);
    RV_EDITOR_CHECK(app.session.number() == number && app.session.connected());
    RV_EDITOR_CHECK(app.session.disc_dir() == disc);
    RV_EDITOR_CHECK(app.release.candidates[0].checks[rv_editor_check_loads].state == rv_editor_check_state::not_run);
}

RV_EDITOR_TEST(editor_playback_candidate_keeps_its_target_when_layout_and_selection_change)
{
    playback_fixture fixture;
    fixture.add_candidate(41, "launched candidate");
    fixture.add_candidate(58, "later selection");
    fixture.switch_to(rv_editor_layout_preset::burn);
    const bool started = fixture.click_run(rv_editor_pane_kind::release_controls, "pane_candidate.run_button");
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }
    auto &app = fixture.app;
    const auto number = app.session.number();
    const auto disc = app.release.candidates[0].image;
    const std::string launch_hash = app.release.candidates[0].sha256;
    RV_EDITOR_CHECK(app.session.facts().medium == "fixed");
    RV_EDITOR_CHECK(app.session.disc_dir() == disc);

    app.release.selected = 1;
    fixture.switch_to(rv_editor_layout_preset::debug);
    RV_EDITOR_CHECK(app.session.number() == number && app.session.connected());
    RV_EDITOR_CHECK(app.session.disc_dir() == disc);
    RV_EDITOR_CHECK(preview_shows(fixture, "Candidate #41") || preview_shows(fixture, "candidate #41"));
    const std::string debug_status = controls_status(fixture, rv_editor_pane_kind::controls);
    RV_EDITOR_CHECK(debug_status.find("candidate #41") != std::string::npos);
    RV_EDITOR_CHECK(shows_session(debug_status, number));

    fixture.switch_to(rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(app.session.number() == number && app.session.connected());
    RV_EDITOR_CHECK(app.session.disc_dir() == disc);
    RV_EDITOR_CHECK(preview_shows(fixture, "Candidate #41") || preview_shows(fixture, "candidate #41"));
    const std::string burn_status = controls_status(fixture, rv_editor_pane_kind::release_controls);
    RV_EDITOR_CHECK(burn_status.find("candidate #41") != std::string::npos);
    RV_EDITOR_CHECK(shows_session(burn_status, number));
    RV_EDITOR_CHECK(fixture.stop());
    const auto &candidate = app.release.candidates[0];
    const auto &loads = candidate.checks[rv_editor_check_loads];
    RV_EDITOR_CHECK(candidate.playtests == 1);
    RV_EDITOR_CHECK(loads.state == rv_editor_check_state::passed);
    RV_EDITOR_CHECK(loads.hash == launch_hash && rv_editor_check_valid(candidate, loads));
    RV_EDITOR_CHECK(app.release.candidates[1].playtests == 0);
    RV_EDITOR_CHECK(app.release.candidates[1].checks[rv_editor_check_loads].state == rv_editor_check_state::not_run);
}

RV_EDITOR_TEST(editor_playback_checks_do_not_validate_replacement_bytes_at_the_same_path)
{
    playback_fixture fixture;
    fixture.add_candidate(41, "original image");
    fixture.switch_to(rv_editor_layout_preset::burn);
    const bool started = fixture.click_run(rv_editor_pane_kind::release_controls, "pane_candidate.run_button");
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }
    auto &candidate = fixture.app.release.candidates[0];
    const auto &loads = candidate.checks[rv_editor_check_loads];
    const std::string launch_hash = candidate.sha256;
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return loads.state == rv_editor_check_state::passed && !candidate.hashing.valid();
    }));

    // Replace the file only after the launch check finished, keeping its path and number.
    const auto replacement = candidate.image.parent_path() / "replacement.mppcdisc";
    std::ofstream(replacement) << "different image bytes";
    std::filesystem::rename(replacement, candidate.image);
    uint64_t size = 0;
    std::string error;
    const std::string replacement_hash = rv_editor_sha256_file(candidate.image, size, error);
    RV_EDITOR_CHECK(!replacement_hash.empty() && replacement_hash != launch_hash);

    fixture.switch_to(rv_editor_layout_preset::scene);
    const auto number = fixture.app.session.number();
    fixture.switch_to(rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(fixture.app.session.number() == number && fixture.app.session.connected());
    RV_EDITOR_CHECK(fixture.stop());
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return candidate.playtests == 1 && !candidate.hashing.valid();
    }));
    RV_EDITOR_CHECK(loads.hash != replacement_hash);
    RV_EDITOR_CHECK(loads.state != rv_editor_check_state::passed || !rv_editor_check_valid(candidate, loads));
}

RV_EDITOR_TEST(editor_playback_pending_verdict_survives_running_a_different_candidate)
{
    playback_fixture fixture;
    auto &app = fixture.app;
    fixture.add_candidate(41, "short first candidate");
    fixture.add_candidate(58, "next candidate");
    fixture.switch_to(rv_editor_layout_preset::burn);
    const bool ended = end_with_hash_pending(fixture);
    RV_EDITOR_CHECK(ended);
    if (!ended) {
        return;
    }

    app.release.selected = 1;
    const auto previous = app.session.number();
    const auto before = app.log.revision();
    const char *why = rv_editor_app_why_not_run_candidate(app);
    const int result = rv_editor_app_run_candidate(app);
    check_run_or_refusal(app, result, previous, why, before);
    if (result == RV_OK) {
        RV_EDITOR_CHECK(pane_state::pump_session(app, [&] {
            return app.session.connected();
        }));
    }
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return !app.release.candidates[0].hashing.valid();
    }));
    check_finished_playtest(fixture);
}

RV_EDITOR_TEST(editor_playback_pending_verdict_survives_running_the_project)
{
    playback_fixture fixture;
    auto &app = fixture.app;
    fixture.add_candidate(41, "short candidate before running the project");
    const bool built = rv_editor_app_build(app) == RV_OK && fixture.wait_until([&] {
        return !rv_editor_app_run_builds(app);
    });
    RV_EDITOR_CHECK(built);
    if (!built) {
        return;
    }
    fixture.switch_to(rv_editor_layout_preset::burn);
    const bool ended = end_with_hash_pending(fixture);
    RV_EDITOR_CHECK(ended);
    if (!ended) {
        return;
    }

    rv_editor_shell_switch(fixture.shell, rv_editor_layout_preset::code);
    const auto previous = app.session.number();
    const auto before = app.log.revision();
    const char *why = rv_editor_app_why_not_run(app);
    const int result = rv_editor_app_run(app);
    check_run_or_refusal(app, result, previous, why, before);
    if (result == RV_OK) {
        RV_EDITOR_CHECK(pane_state::pump_session(app, [&] {
            return app.session.connected();
        }));
    }
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return !app.release.candidates[0].hashing.valid();
    }));
    check_finished_playtest(fixture);
}

RV_EDITOR_TEST(editor_playback_and_build_progress_fit_at_both_controls_minima)
{
    playback_fixture fixture(true);
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    auto &app = fixture.app;
    fixture.add_candidate(999998, "playing a six-digit candidate");
    fixture.switch_to(rv_editor_layout_preset::burn);
    const bool started = fixture.click_run(rv_editor_pane_kind::release_controls, "pane_candidate.run_button");
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }

    pane_state::write_script(app.tools.burner.path,
        "echo 'mppcburner: [99/99] xxxxxxxxxxxxxxxx detail' >&2\n"
        "exec sleep 30\n");
    const bool building = rv_editor_app_build_candidate(app) == RV_OK && fixture.wait_until([&] {
        return app.build.busy() && !app.build.progress().runs.empty();
    });
    RV_EDITOR_CHECK(building);
    if (!building) {
        return;
    }
    RV_EDITOR_CHECK(app.build.target_number() == 999999);
    const auto session = app.session.number();
    const auto check_controls = [&](rv_editor_pane_kind pane, const char *name) {
        const auto min = stable_tiles::pane_minimum(fixture.gui, pane);
        RV_EDITOR_CHECK(min.w > 0 && min.h > 0);
        const std::string session_text = "session " + std::to_string(session);
        for (const char *text : { session_text.c_str(), "Candidate #999998", "Candidate #999999", "99/99", "xxxxxxxxxxxxxxxx" }) {
            const bool visible = controls_show(fixture, pane, min, text);
            if (!visible) {
                std::fprintf(stderr, "  %s at %.0fx%.0f does not show '%s' whole\n", name, min.w, min.h, text);
            }
            RV_EDITOR_CHECK(visible);
        }
    };

    check_controls(rv_editor_pane_kind::release_controls, "Release Controls");
    fixture.switch_to(rv_editor_layout_preset::debug);
    check_controls(rv_editor_pane_kind::controls, "Runtime Controls");
    std::string error;
    RV_EDITOR_CHECK(app.build.cancel(error) == RV_OK);
    RV_EDITOR_CHECK(fixture.wait_until([&] { return !app.build.busy() && !app.release.building; }));
}

RV_EDITOR_TEST(rv_editor_test_interactive_manual_waits_for_a_successful_build_and_run)
{
    playback_fixture fixture;
    auto &app = fixture.app;
    using step = rv_editor_manual_step;
    const bool built = rv_editor_app_build(app) == RV_OK && fixture.wait_until([&] {
        return app.build.last_success().has_value();
    });
    RV_EDITOR_CHECK(built);
    if (!built) {
        return;
    }
    rv_editor_shell_manual_start(fixture.shell, step::build_project);
    rv_editor_shell_update(fixture.shell);
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::build_project);

    pane_state::write_script(app.tools.burner.path, "exit 1\n");
    RV_EDITOR_CHECK(rv_editor_app_build(app) == RV_OK);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return app.build.state() == rv_editor_build_state::failed;
    }));
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::build_project);

    pane_state::write_script(app.tools.burner.path, "mkdir -p \"$4\"\n");
    RV_EDITOR_CHECK(rv_editor_app_build(app) == RV_OK);
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::build_project);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return app.build.state() == rv_editor_build_state::succeeded;
    }));
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::run_project);

    fixture.add_candidate(42, "candidate");
    fixture.switch_to(rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(rv_editor_app_run_candidate(app) == RV_OK);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return app.session.state() == rv_editor_run_state::running;
    }));
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::run_project);
    RV_EDITOR_CHECK(fixture.stop());

    fixture.switch_to(rv_editor_layout_preset::code);
    RV_EDITOR_CHECK(rv_editor_app_run(app) == RV_OK);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return app.session.state() == rv_editor_run_state::running;
    }));
    RV_EDITOR_CHECK(fixture.shell.manual.step == step::complete);
}
