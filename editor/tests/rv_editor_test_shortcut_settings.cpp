// S8, AC14: global editor shortcuts keep their defaults and can be changed in Settings.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

#include "app/rv_editor_shell.hpp"
#include "app/rv_editor_shell_shortcut_settings.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "platform/rv_editor_process.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;

constexpr ImGuiKeyChord new_project_default = ImGuiMod_Ctrl | ImGuiKey_N;
constexpr ImGuiKeyChord manual_default = ImGuiKey_F1;
constexpr ImGuiKeyChord manual_custom = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_M;
constexpr ImGuiKeyChord build_default = ImGuiMod_Ctrl | ImGuiKey_B;
constexpr ImGuiKeyChord save_all_default = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S;

struct config_fixture {
    const fs::path dir = fs::temp_directory_path() / ("rv_editor_shortcut_settings_" + std::to_string(getpid()));
    const fs::path file = dir / "3dmppc-editor" / "settings.toml";
    std::optional<std::string> previous_config;

    config_fixture()
    {
        if (const char *previous = std::getenv("XDG_CONFIG_HOME")) {
            previous_config = previous;
        }
        fs::create_directories(file.parent_path());
        RV_EDITOR_CHECK(::setenv("XDG_CONFIG_HOME", dir.c_str(), 1) == 0);
    }

    ~config_fixture()
    {
        if (previous_config) {
            ::setenv("XDG_CONFIG_HOME", previous_config->c_str(), 1);
        } else {
            ::unsetenv("XDG_CONFIG_HOME");
        }
        std::error_code error;
        fs::remove_all(dir, error);
    }
};

void open_settings(rv_editor_test_imgui &gui, rv_editor_shell &shell)
{
    rv_editor_shell_page(shell, rv_editor_start_page::settings);
    gui.frame(
        [&] {
            rv_editor_page_settings(shell, rv_editor_theme_olive);
        },
        1200.0f,
        1000.0f);
}

void apply_settings(rv_editor_shell &shell)
{
    shell.settings_apply_request = true;
    rv_editor_shell_settings_apply(shell);
    RV_EDITOR_CHECK(shell.settings_error.empty());
}

template <class Draw>
void frame_with_chord(rv_editor_test_imgui &gui, ImGuiKeyChord chord, Draw draw)
{
    ImGuiIO &io = ImGui::GetIO();
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    io.AddKeyEvent(ImGuiMod_Ctrl, (chord & ImGuiMod_Ctrl) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (chord & ImGuiMod_Shift) != 0);
    io.AddKeyEvent(key, true);
    gui.frame(draw);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddKeyEvent(key, false);
    gui.frame([] {});
}

void press_chord(rv_editor_test_imgui &gui, rv_editor_shell &shell, ImGuiKeyChord chord, bool text_input = false)
{
    frame_with_chord(gui, chord, [&] {
        ImGui::GetIO().WantTextInput = text_input;
        rv_editor_shell_shortcuts(shell);
    });
}

void remap_build_and_save_all(rv_editor_test_imgui &gui, rv_editor_shell &shell)
{
    open_settings(gui, shell);
    std::string error;
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::build, ImGuiKey_F10, error));
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::save_all, ImGuiKey_F11, error));
    apply_settings(shell);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_clean_config_defaults_and_controls)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);

    press_chord(gui, shell, new_project_default, true);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::recent);
    press_chord(gui, shell, new_project_default);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::new_project);
    press_chord(gui, shell, manual_default, true);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::manual);

    std::string settings_text;
    gui.frame(
        [&] {
            ImGui::LogToBuffer(0);
            rv_editor_page_settings(shell, rv_editor_theme_olive);
            settings_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        },
        1200.0f,
        1000.0f);
    RV_EDITOR_CHECK(settings_text.find(rv_editor_text("shell_settings.pace_title")) != std::string::npos);
    RV_EDITOR_CHECK(settings_text.find(rv_editor_text("shell_menu.keyboard_shortcuts")) != std::string::npos);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_preserve_legacy_config)
{
    config_fixture config;
    const auto legacy_console = config.dir / "legacy console";
    {
        std::ofstream legacy(config.file);
        legacy << "[tools]\nconsole = \"" << legacy_console.string() << "\"\n";
    }
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);
    open_settings(gui, shell);
    RV_EDITOR_CHECK(shell.app.tools.console.origin == "settings.toml");
    RV_EDITOR_CHECK(shell.app.tools.console.path == legacy_console);

    std::string error;
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, manual_custom, error));
    apply_settings(shell);

    rv_editor_shell restarted;
    rv_editor_app_init(restarted.app);
    RV_EDITOR_CHECK(restarted.app.tools.console.origin == "settings.toml");
    RV_EDITOR_CHECK(restarted.app.tools.console.path == legacy_console);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_reassignment_survives_restart)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    {
        rv_editor_shell shell;
        rv_editor_app_init(shell.app);
        open_settings(gui, shell);
        std::string error;
        RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, manual_custom, error));
        apply_settings(shell);
    }

    rv_editor_shell restarted;
    rv_editor_app_init(restarted.app);
    press_chord(gui, restarted, manual_default);
    RV_EDITOR_CHECK(restarted.start_page == rv_editor_start_page::recent);
    restarted.start_page = rv_editor_start_page::recent;
    press_chord(gui, restarted, ImGuiMod_Ctrl | ImGuiKey_M);
    RV_EDITOR_CHECK(restarted.start_page == rv_editor_start_page::recent);
    press_chord(gui, restarted, manual_custom);
    RV_EDITOR_CHECK(restarted.start_page == rv_editor_start_page::manual);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_reject_conflict_before_apply)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);
    open_settings(gui, shell);

    std::string error;
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, manual_custom, error));
    RV_EDITOR_CHECK(!rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, new_project_default, error));
    RV_EDITOR_CHECK(!error.empty());
    RV_EDITOR_CHECK(!fs::exists(config.file));
    press_chord(gui, shell, manual_custom);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::settings);
    apply_settings(shell);

    press_chord(gui, shell, new_project_default);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::new_project);
    press_chord(gui, shell, manual_custom);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::manual);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_restore_defaults_across_restart)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    {
        rv_editor_shell shell;
        rv_editor_app_init(shell.app);
        open_settings(gui, shell);
        std::string error;
        RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, manual_custom, error));
        apply_settings(shell);
        press_chord(gui, shell, manual_custom);
        RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::manual);
        open_settings(gui, shell);
        rv_editor_shell_shortcuts_reset(shell);
        apply_settings(shell);
    }

    rv_editor_shell restarted;
    rv_editor_app_init(restarted.app);
    press_chord(gui, restarted, manual_custom);
    RV_EDITOR_CHECK(restarted.start_page == rv_editor_start_page::recent);
    press_chord(gui, restarted, manual_default);
    RV_EDITOR_CHECK(restarted.start_page == rv_editor_start_page::manual);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_reject_window_navigation_conflicts)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);
    open_settings(gui, shell);

    std::string error;
    RV_EDITOR_CHECK(rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, manual_custom, error));
    RV_EDITOR_CHECK(
        !rv_editor_shell_shortcut_assign(shell, rv_editor_shortcut_action::manual, ImGuiMod_Ctrl | ImGuiKey_Tab, error));
    RV_EDITOR_CHECK(error.find("Tab") != std::string::npos);
    error.clear();
    RV_EDITOR_CHECK(!rv_editor_shell_shortcut_assign(shell,
        rv_editor_shortcut_action::manual,
        ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab,
        error));
    RV_EDITOR_CHECK(error.find("Tab") != std::string::npos);
    RV_EDITOR_CHECK(!fs::exists(config.file));
    apply_settings(shell);
    press_chord(gui, shell, manual_custom);
    RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::manual);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_reassigned_chords_route_to_code)
{
    config_fixture config;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);
    const auto code_input = [&](ImGuiKeyChord chord) {
        std::string input;
        frame_with_chord(gui, chord, [&] {
            rv_editor_shell_shortcuts(shell);
            input = rv_editor_nvim_keys();
        });
        return input;
    };

    RV_EDITOR_CHECK(code_input(build_default).empty());
    RV_EDITOR_CHECK(code_input(save_all_default).empty());
    remap_build_and_save_all(gui, shell);

    const auto scene_path = config.dir / "courtyard.scene.toml";
    shell.app.project.root = config.dir;
    shell.app.scene = std::make_unique<rv_editor_scene_doc>();
    auto &scene = *shell.app.scene;
    scene.scene.path = scene_path;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_scene_save(scene.scene, error) == RV_OK);
    rv_editor_scene_object tree;
    tree.id = "tree";
    rv_editor_scene_step(scene);
    scene.scene.objects.push_back(tree);

    shell.app.build_alert = {};
    RV_EDITOR_CHECK(code_input(build_default) == "<C-b>");
    RV_EDITOR_CHECK(code_input(save_all_default) == "<S-C-s>");
    RV_EDITOR_CHECK(shell.app.build_alert.kind == rv_editor_build_alert_kind::none);
    rv_editor_scene saved_scene;
    RV_EDITOR_CHECK(rv_editor_scene_load(scene_path, saved_scene, error) == RV_OK);
    RV_EDITOR_CHECK(saved_scene.objects.empty());
    RV_EDITOR_CHECK(code_input(ImGuiKey_F11).empty());
    RV_EDITOR_CHECK(rv_editor_scene_load(scene_path, saved_scene, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_scene_find(saved_scene, tree.id) >= 0);
    RV_EDITOR_CHECK(code_input(ImGuiKey_F10).empty());
    RV_EDITOR_CHECK(shell.app.build_alert.kind == rv_editor_build_alert_kind::not_started);
}

RV_EDITOR_TEST(rv_editor_test_shortcut_settings_reassigned_chords_route_to_terminal)
{
    config_fixture config;
    rv_editor_test_imgui gui(true);
    // This test context borrows its atlas, so ImGui does not advance it.
    ImFontAtlasUpdateNewFrame(ImGui::GetIO().Fonts, 0, true);
    rv_editor_shell shell;
    rv_editor_app_init(shell.app);
    remap_build_and_save_all(gui, shell);

    std::string error;
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);

    const auto probe = config.dir / "terminal-probe";
    const auto recorded_input = config.dir / "terminal-input";
    {
        std::ofstream script(probe);
        script << "#!/bin/sh\nstty raw -echo || exit 1\nexec cat > terminal-input\n";
    }
    fs::permissions(probe, fs::perms::owner_exec, fs::perm_options::add);

    constexpr rv_editor_pane_id pane = 1;
    auto &view = shell.app.terminals[pane];
    view.term = std::make_unique<rv_editor_terminal>();
    const char *previous = std::getenv("SHELL");
    const std::optional<std::string> saved_shell = previous == nullptr ? std::nullopt : std::optional<std::string>(previous);
    RV_EDITOR_CHECK(::setenv("SHELL", probe.c_str(), 1) == 0);
    const int started = view.term->start(config.dir, 80, 24, rv_editor_theme_olive, error);
    if (saved_shell) {
        ::setenv("SHELL", saved_shell->c_str(), 1);
    } else {
        ::unsetenv("SHELL");
    }
    if (started != RV_OK) {
        std::fprintf(stderr, "terminal probe: %s\n", error.c_str());
        RV_EDITOR_CHECK(started == RV_OK);
        return;
    }

    const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!fs::exists(recorded_input) && std::chrono::steady_clock::now() < ready_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RV_EDITOR_CHECK(fs::exists(recorded_input));
    if (!fs::exists(recorded_input)) {
        return;
    }

    const auto input = [&](ImGuiKeyChord chord) {
        frame_with_chord(gui, chord, [&] {
            rv_editor_shell_shortcuts(shell);
            view.focus_request = true;
            rv_editor_pane_terminal(shell.app, pane, rv_editor_theme_olive, { shell.ui, shell.ui_requests });
        });
    };
    input(ImGuiKey_F10);
    input(ImGuiKey_F11);
    input(build_default);
    input(save_all_default);
    // A sentinel establishes that the focused pane sent input and all preceding bytes arrived.
    ImGui::GetIO().AddInputCharacter('x');
    input(ImGuiKey_X);

    std::string bytes;
    const auto input_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        RV_EDITOR_CHECK(view.term->update(error) == RV_OK);
        std::ifstream file(recorded_input, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), {});
        if (bytes.ends_with('x')) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < input_deadline);
    RV_EDITOR_CHECK(bytes.ends_with('x'));
    if (bytes != "\x02\x13x") {
        std::fprintf(stderr, "terminal probe received %zu bytes, expected Ctrl+B, Ctrl+S and x\n", bytes.size());
    }
    RV_EDITOR_CHECK(bytes == "\x02\x13x");
}
