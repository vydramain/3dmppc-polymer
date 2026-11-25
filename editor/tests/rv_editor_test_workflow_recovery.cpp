// AC12 and AC13: recovery actions are available from their tiles.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "app/rv_editor_shell_shortcut_settings.hpp"
#include "project/rv_editor_settings.hpp"
#include "rv_editor_test_workflow_playback_fixture.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using rv_editor_test::workflow_playback::playback_fixture;

namespace
{

constexpr auto run_last_label = "Run Last Successful Build";
constexpr auto force_stop_label = "Force Stop";

std::optional<ImVec2> button_at(playback_fixture &fixture, rv_editor_pane_kind pane, std::string_view label)
{
    fixture.draw(pane);
    const auto drawn = fixture.draw(pane);
    const auto item = std::ranges::find_if(drawn.entries, [&](const auto &entry) {
        const std::string_view visible = std::string_view(entry.label).substr(0, entry.label.find("##"));
        return entry.id != 0 &&
            (visible == label || (visible.starts_with(label) && visible.substr(label.size()).starts_with(" (")));
    });
    if (item == drawn.entries.end()) {
        return std::nullopt;
    }
    return ImVec2((item->x0 + item->x1) / 2, (item->y0 + item->y1) / 2);
}

void click(playback_fixture &fixture, rv_editor_pane_kind pane, ImVec2 at)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    fixture.draw(pane);
    io.AddMouseButtonEvent(0, true);
    fixture.draw(pane);
    io.AddMouseButtonEvent(0, false);
    fixture.draw(pane);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    rv_editor_shell_update(fixture.shell);
}

std::string text_after_hover(playback_fixture &fixture, rv_editor_pane_kind pane, ImVec2 at)
{
    ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    std::string text;
    // Allow time for either the standard delayed tooltip or an immediate disabled reason.
    for (int i = 0; i < 50; ++i) {
        fixture.draw(pane);
    }
    fixture.gui.frame(
        [&] {
            ImGui::LogToBuffer(0);
            rv_editor_shell_pane(&fixture.shell, 1, pane, stable_tiles::theme);
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        },
        workflow_playback::pane_width,
        workflow_playback::pane_height);
    return text;
}

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream file(path);
    return { std::istreambuf_iterator<char>(file), {} };
}

// Stand in for the console at its process boundary, with optional failure to acknowledge quit.
void write_console(playback_fixture &fixture, bool hang, std::string_view on_start = {})
{
    pane_state::write_script(fixture.app.tools.console.path,
        "for disc do :; done\n"
        "case \"$disc\" in *.mppcdisc) medium=fixed;; *) medium=live;; esac\n" +
            std::string(on_start) +
            "mode=running\n"
            "while read -r id verb rest; do\n"
            "  case \"$verb\" in\n"
            "    status) echo \"$id ok protocol=" +
            std::string(rv_editor_session::protocol_supported) +
            " mode=$mode disc=64697363 pdk=1.0 medium=$medium entry_revision=1\";;\n"
            "    pause) mode=paused; echo \"$id ok mode=$mode\";;\n"
            "    resume) mode=running; echo \"$id ok mode=$mode\";;\n"
            "    quit) echo quit > quit-received; " +
            (hang ? std::string(":") : std::string("echo \"$id ok\"; exit 0")) +
            ";;\n"
            "  esac\n"
            "done\n");
}

bool start_session(playback_fixture &fixture, rv_editor_pane_kind pane)
{
    if (pane == rv_editor_pane_kind::release_controls) {
        fixture.add_candidate(42, "selected candidate");
        fixture.switch_to(rv_editor_layout_preset::burn);
        return fixture.click_run(pane, "pane_candidate.run_button");
    }
    fixture.switch_to(rv_editor_layout_preset::debug);
    return fixture.click_run(pane, "widgets_status.button_run");
}

void check_stop_and_idle_reason(rv_editor_pane_kind pane)
{
    playback_fixture fixture;
    write_console(fixture, false);
    const bool started = start_session(fixture, pane);
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }
    const auto stop = button_at(fixture, pane, "Stop");
    RV_EDITOR_CHECK(stop.has_value());
    if (!stop) {
        return;
    }
    click(fixture, pane, *stop);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return !fixture.app.session.live();
    }));
    RV_EDITOR_CHECK(read_file(fixture.root / "quit-received") == "quit\n");
    RV_EDITOR_CHECK(fixture.app.session.end_reason() == "stopped");

    const auto force_stop = button_at(fixture, pane, force_stop_label);
    RV_EDITOR_CHECK(force_stop.has_value());
    if (force_stop) {
        const std::string explanation = text_after_hover(fixture, pane, *force_stop);
        RV_EDITOR_CHECK(explanation.find("No runtime is running") != std::string::npos ||
            explanation.find("No session is running") != std::string::npos);
        const auto before = fixture.app.log.revision();
        click(fixture, pane, *force_stop);
        RV_EDITOR_CHECK(fixture.app.log.revision() == before);
    }
}

void check_force_stop(rv_editor_pane_kind pane, bool hang)
{
    playback_fixture fixture;
    write_console(fixture, hang);
    const bool started = start_session(fixture, pane);
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }

    if (hang) {
        // Arrange a process that ignores quit and wait for the app to detect it.
        RV_EDITOR_CHECK(rv_editor_app_stop(fixture.app) == RV_OK);
        const auto is_hung = [&] {
            return fixture.app.session.hung();
        };
        const bool hung = fixture.wait_until(is_hung) || fixture.wait_until(is_hung);
        RV_EDITOR_CHECK(hung);
    } else {
        RV_EDITOR_CHECK(fixture.app.session.state() == rv_editor_run_state::running);
    }

    const auto force_stop = button_at(fixture, pane, force_stop_label);
    RV_EDITOR_CHECK(force_stop.has_value());
    if (force_stop) {
        click(fixture, pane, *force_stop);
        RV_EDITOR_CHECK(fixture.wait_until([&] {
            return !fixture.app.session.live();
        }));
        RV_EDITOR_CHECK(fixture.app.session.end_reason().starts_with(end_reason_force_stopped));
        if (!hang) {
            RV_EDITOR_CHECK(!std::filesystem::exists(fixture.root / "quit-received"));
        }
    }
    if (fixture.app.session.live() && hang) {
        rv_editor_app_force_stop(fixture.app);
        fixture.wait_until([&] {
            return !fixture.app.session.live();
        });
    }
}

void press_chord(playback_fixture &fixture, ImGuiKeyChord chord)
{
    ImGuiIO &io = ImGui::GetIO();
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    io.AddKeyEvent(ImGuiMod_Ctrl, (chord & ImGuiMod_Ctrl) != 0);
    io.AddKeyEvent(key, true);
    fixture.gui.frame([&] {
        rv_editor_shell_shortcuts(fixture.shell);
    });
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(key, false);
    fixture.gui.frame([] {});
}

void check_remapped_run_shortcut(rv_editor_pane_kind pane)
{
    playback_fixture fixture;
    if (pane == rv_editor_pane_kind::release_controls) {
        fixture.add_candidate(42, "selected candidate");
        fixture.switch_to(rv_editor_layout_preset::burn);
    }
    fixture.app.tools.settings_path = fixture.root / "editor.toml";
    std::string error;
    const rv_editor_settings_entries shortcuts = { { "run", "F10" } };
    RV_EDITOR_CHECK(rv_editor_settings_save(fixture.app.tools.settings_path, {}, shortcuts, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_chord(fixture.shell, rv_editor_shortcut_action::run) == ImGuiKey_F10);

    const auto run = button_at(fixture, pane, "Run");
    RV_EDITOR_CHECK(run.has_value());
    if (run) {
        const auto text = text_after_hover(fixture, pane, *run);
        RV_EDITOR_CHECK(text.find("F10") != std::string::npos);
        RV_EDITOR_CHECK(text.find("F5") == std::string::npos);
    }
}

void check_burn_shortcut_resumes(rv_editor_pane_kind origin)
{
    playback_fixture fixture;
    write_console(fixture, false);
    const bool started = start_session(fixture, origin);
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }

    auto &session = fixture.app.session;
    const auto number = session.number();
    const auto process = session.pid();
    const auto disc = session.disc_dir();
    press_chord(fixture, ImGuiKey_F6);
    const bool paused = fixture.wait_until([&] {
        return session.state() == rv_editor_run_state::paused;
    });
    RV_EDITOR_CHECK(paused);
    if (!paused) {
        return;
    }

    fixture.add_candidate(58, "another candidate");
    fixture.app.release.selected = fixture.app.release.candidates.size() - 1;
    fixture.switch_to(rv_editor_layout_preset::burn);
    press_chord(fixture, ImGuiKey_F5);
    const bool resumed = fixture.wait_until([&] {
        return session.state() == rv_editor_run_state::running;
    });
    RV_EDITOR_CHECK(resumed);
    RV_EDITOR_CHECK(session.number() == number && session.pid() == process && session.disc_dir() == disc);
}

void check_off_route_shortcut_tooltips(rv_editor_pane_kind pane,
    rv_editor_layout_preset matching_layout,
    rv_editor_layout_preset other_layout)
{
    playback_fixture fixture;
    fixture.add_candidate(42, "selected candidate");
    fixture.app.tools.settings_path = fixture.root / "editor.toml";
    const rv_editor_settings_entries shortcuts = { { "build", "F11" }, { "run", "F10" } };
    std::string error;
    RV_EDITOR_CHECK(rv_editor_settings_save(fixture.app.tools.settings_path, {}, shortcuts, error) == RV_OK);

    fixture.switch_to(matching_layout);
    const auto build = button_at(fixture, pane, "Build");
    const auto run = button_at(fixture, pane, "Run");
    RV_EDITOR_CHECK(build.has_value() && run.has_value());
    if (!build || !run) {
        return;
    }
    RV_EDITOR_CHECK(text_after_hover(fixture, pane, *build).find("(F11)") != std::string::npos);
    RV_EDITOR_CHECK(text_after_hover(fixture, pane, *run).find("(F10)") != std::string::npos);

    fixture.switch_to(other_layout);
    rv_editor_shell_show_pane(fixture.shell, pane);
    const auto moved_build = button_at(fixture, pane, "Build");
    const auto moved_run = button_at(fixture, pane, "Run");
    RV_EDITOR_CHECK(moved_build.has_value() && moved_run.has_value());
    if (!moved_build || !moved_run) {
        return;
    }
    const auto build_tooltip = text_after_hover(fixture, pane, *moved_build);
    const auto run_tooltip = text_after_hover(fixture, pane, *moved_run);
    RV_EDITOR_CHECK(build_tooltip.find("(F11)") == std::string::npos);
    RV_EDITOR_CHECK(build_tooltip.find("(Ctrl+B)") == std::string::npos);
    RV_EDITOR_CHECK(run_tooltip.find("(F10)") == std::string::npos);
    RV_EDITOR_CHECK(run_tooltip.find("(F5)") == std::string::npos);
}

} // namespace

RV_EDITOR_TEST(editor_build_result_runs_last_native_build_with_current_script_and_asset)
{
    playback_fixture fixture;
    auto &app = fixture.app;
    std::filesystem::create_directories(fixture.root / "scripts");
    const auto script = fixture.root / "scripts/main.lua";
    std::ofstream(script) << "return 'original'\n";
    std::filesystem::create_directories(fixture.root / "data");
    const auto asset = fixture.root / "data/config.txt";
    std::ofstream(asset) << "original asset\n";
    // Unpacked output keeps a snapshot of the native module and live links to project scripts and copied assets.
    pane_state::write_script(app.tools.burner.path,
        "test \"$1\" = build && test \"$3\" = --unpacked || exit 1\n"
        "echo build >> native-builds\n"
        "mkdir -p \"$4/scripts\" \"$4/data\"\n"
        "printf native-v1 > \"$4/disc.so\"\n"
        "ln -s \"$2/scripts/main.lua\" \"$4/scripts/main.lua\"\n"
        "ln -s \"$2/data/config.txt\" \"$4/data/config.txt\"\n");
    const bool built = rv_editor_app_build(app) == RV_OK && fixture.wait_until([&] {
        return app.build.last_success().has_value();
    });
    RV_EDITOR_CHECK(built);
    if (!built) {
        return;
    }
    const auto artifact = *app.build.last_success();

    std::ofstream(script) << "return 'edited'\n";
    std::ofstream(asset) << "edited asset\n";
    app.files.changed = { script, asset };
    RV_EDITOR_CHECK(rv_editor_app_changed_files_apply(app) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_app_run_builds(app));

    write_console(fixture,
        false,
        "cat \"$disc/disc.so\" \"$disc/scripts/main.lua\" \"$disc/data/config.txt\" > playback-inputs\n");

    const auto run_last = button_at(fixture, rv_editor_pane_kind::build_result, run_last_label);
    RV_EDITOR_CHECK(run_last.has_value());
    if (run_last) {
        click(fixture, rv_editor_pane_kind::build_result, *run_last);
        RV_EDITOR_CHECK(fixture.wait_until([&] {
            return app.session.connected();
        }));
        RV_EDITOR_CHECK(app.session.build_number() == artifact.number && app.session.disc_dir() == artifact.dir);
        RV_EDITOR_CHECK(read_file(fixture.root / "playback-inputs") == "native-v1return 'edited'\nedited asset\n");
        RV_EDITOR_CHECK(read_file(fixture.root / "native-builds") == "build\n");
        RV_EDITOR_CHECK(!app.build.busy());
    }
}

RV_EDITOR_TEST(editor_build_result_explains_unavailable_last_build)
{
    playback_fixture fixture;
    const auto pane = rv_editor_pane_kind::build_result;
    const auto run_last = button_at(fixture, pane, run_last_label);
    RV_EDITOR_CHECK(run_last.has_value());
    if (run_last) {
        RV_EDITOR_CHECK(text_after_hover(fixture, pane, *run_last).find(rv_editor_text("app_why_not.no_build_succeeded")) !=
            std::string::npos);
        const auto before = fixture.app.log.revision();
        click(fixture, pane, *run_last);
        RV_EDITOR_CHECK(!fixture.app.session.live() && !fixture.app.build.busy());
        RV_EDITOR_CHECK(fixture.app.log.revision() == before);
    }
}

RV_EDITOR_TEST(editor_runtime_controls_graceful_stop_and_idle_force_stop)
{
    check_stop_and_idle_reason(rv_editor_pane_kind::controls);
}

RV_EDITOR_TEST(editor_release_controls_graceful_stop_and_idle_force_stop)
{
    check_stop_and_idle_reason(rv_editor_pane_kind::release_controls);
}

RV_EDITOR_TEST(editor_runtime_controls_force_stop_running_and_hung_sessions)
{
    check_force_stop(rv_editor_pane_kind::controls, false);
    check_force_stop(rv_editor_pane_kind::controls, true);
}

RV_EDITOR_TEST(editor_release_controls_force_stop_running_and_hung_sessions)
{
    check_force_stop(rv_editor_pane_kind::release_controls, false);
    check_force_stop(rv_editor_pane_kind::release_controls, true);
}

RV_EDITOR_TEST(editor_burn_standard_shortcuts_build_and_run_a_candidate)
{
    playback_fixture fixture;
    auto &app = fixture.app;
    fixture.switch_to(rv_editor_layout_preset::burn);
    pane_state::write_script(app.tools.burner.path,
        "case \"$3\" in -o) printf candidate > \"$4\";; --unpacked) mkdir -p \"$4\";; *) exit 1;; esac\n");

    press_chord(fixture, ImGuiMod_Ctrl | ImGuiKey_B);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return !app.build.busy() && !app.release.building;
    }));
    RV_EDITOR_CHECK(app.release.candidates.size() == 1);
    RV_EDITOR_CHECK(!app.build.last_success().has_value());

    if (app.release.candidates.empty()) {
        fixture.add_candidate(42, "selected candidate");
    }
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return rv_editor_app_why_not_run_candidate(app) == nullptr;
    }));
    const auto expected_image = app.release.candidates[app.release.selected].image;
    press_chord(fixture, ImGuiKey_F5);
    RV_EDITOR_CHECK(fixture.wait_until([&] {
        return app.session.connected();
    }));
    RV_EDITOR_CHECK(app.session.disc_dir() == expected_image);
}

RV_EDITOR_TEST(editor_runtime_controls_show_the_remapped_run_shortcut)
{
    check_remapped_run_shortcut(rv_editor_pane_kind::controls);
}

RV_EDITOR_TEST(editor_release_controls_show_the_remapped_run_shortcut)
{
    check_remapped_run_shortcut(rv_editor_pane_kind::release_controls);
}

RV_EDITOR_TEST(editor_burn_run_shortcut_resumes_the_paused_candidate)
{
    check_burn_shortcut_resumes(rv_editor_pane_kind::release_controls);
}

RV_EDITOR_TEST(editor_burn_run_shortcut_resumes_the_project_paused_before_switching_layout)
{
    check_burn_shortcut_resumes(rv_editor_pane_kind::controls);
}

RV_EDITOR_TEST(editor_runtime_controls_in_burn_omit_candidate_shortcuts)
{
    check_off_route_shortcut_tooltips(rv_editor_pane_kind::controls,
        rv_editor_layout_preset::code,
        rv_editor_layout_preset::burn);
}

RV_EDITOR_TEST(editor_release_controls_in_code_omit_project_shortcuts)
{
    check_off_route_shortcut_tooltips(rv_editor_pane_kind::release_controls,
        rv_editor_layout_preset::burn,
        rv_editor_layout_preset::code);
}
