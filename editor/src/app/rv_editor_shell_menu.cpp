// The window's menu bar (spec LAY-07): File, Edit, View, Project, Run, Window,
// Help, the layout switch at its right end, and the shortcuts behind them.

#include "app/rv_editor_shell.hpp"

#include <cmath>
#include <string>

#include "imgui.h"

#include "font/rv_editor_font.hpp"

namespace rv_editor
{

bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not)
{
    const bool clicked = ImGui::MenuItem(label, shortcut, false, why_not == nullptr);
    if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", why_not);
    }
    return clicked;
}

namespace
{

// A top-level menu with its mnemonic, the first letter, underlined (UX-06). Alt
// moves the keyboard into the bar; the arrows walk it.
bool rv_editor_menu_begin(const char *label)
{
    const bool open = ImGui::BeginMenu(label);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float text = ImGui::CalcTextSize(label).x;
    const float x = std::floor(min.x + (max.x - min.x - text) / 2.0f);
    const char first[2] = { label[0], '\0' };
    // Below the baseline; the bar's own clip would cut it, so it goes on top.
    const float y = std::floor(min.y + (max.y - min.y + ImGui::GetFontSize()) / 2.0f) - 1.0f;
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + ImGui::CalcTextSize(first).x, y + 1.0f),
        ImGui::GetColorU32(ImGuiCol_Text));
    return open;
}

// The code tile used last, and its nvim window, for Save, Undo and Redo; 0 when
// there is none.
int64_t rv_editor_menu_code_window(rv_editor_shell &shell)
{
    if (shell.last_code == rv_editor_tile_none || !shell.app.nvim.running()) {
        return 0;
    }
    return shell.app.nvim.window_for(shell.last_code);
}

// Keys to the code tile used last, as if typed there: the editor's nvim config
// maps them (editor/nvim/rv_editor_init.lua).
void rv_editor_menu_code_keys(rv_editor_shell &shell, const char *keys)
{
    const int64_t win = rv_editor_menu_code_window(shell);
    if (win != 0) {
        shell.app.nvim.focus(win);
        shell.app.nvim.input(keys);
    }
}

void rv_editor_menu_file(rv_editor_shell &shell)
{
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? "No code tile yet: Window > New Tile > Code" : nullptr;
    if (rv_editor_menu_item("New File", nullptr, no_code)) {
        rv_editor_menu_code_keys(shell, "<Cmd>enew<CR>");
    }
    if (ImGui::MenuItem("Open Directory...")) {
        rv_editor_shell_open_folder(shell);
    }
    if (ImGui::MenuItem("Open disc.toml...")) {
        rv_editor_shell_open_manifest(shell);
    }
    ImGui::Separator();
    if (rv_editor_menu_item("Save", "Ctrl+S", no_code)) {
        rv_editor_menu_code_keys(shell, "<C-s>");
    }
    if (ImGui::MenuItem("Save All", "Ctrl+Shift+S")) {
        rv_editor_shell_save_all(shell);
    }
    if (ImGui::MenuItem("Save As...")) {
        rv_editor_shell_save_as_start(shell);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Settings...")) {
        shell.settings_open = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
}

void rv_editor_menu_edit(rv_editor_shell &shell)
{
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? "No code tile to undo in" : nullptr;
    if (rv_editor_menu_item("Undo", "Ctrl+Z", no_code)) {
        rv_editor_menu_code_keys(shell, "<C-z>");
    }
    if (rv_editor_menu_item("Redo", "Ctrl+Shift+Z", no_code)) {
        rv_editor_menu_code_keys(shell, "<C-S-z>");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Find in Project", "Ctrl+Shift+F")) {
        shell.app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
}

void rv_editor_menu_view(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    if (ImGui::BeginMenu("Code Text Size")) {
        constexpr struct
        {
            rv_editor_code_size size;
            const char *label;
        } sizes[] = { { rv_editor_code_size::small, "Small (8x14)" }, { rv_editor_code_size::normal, "Normal (9x16)" },
            { rv_editor_code_size::large, "Large (9x16, doubled)" } };
        for (const auto &s : sizes) {
            if (ImGui::MenuItem(s.label, nullptr, rv_editor_font_code_size() == s.size)) {
                rv_editor_font_code_size_set(s.size);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Game Scale")) {
        constexpr struct
        {
            rv_editor_game_scale scale;
            const char *label;
        } scales[] = { { rv_editor_game_scale::fit, "Fit" }, { rv_editor_game_scale::integer, "Integer" },
            { rv_editor_game_scale::x1, "1x" }, { rv_editor_game_scale::x2, "2x" }, { rv_editor_game_scale::x3, "3x" } };
        for (const auto &s : scales) {
            if (ImGui::MenuItem(s.label, nullptr, app.game_scale == s.scale)) {
                app.game_scale = s.scale;
            }
        }
        ImGui::EndMenu();
    }
    // Independent of the code text size and of the Game scale (VIS-03).
    if (ImGui::BeginMenu("UI Scale")) {
        constexpr struct
        {
            float scale;
            const char *label;
        } scales[] = { { 1.0f, "1x" }, { 2.0f, "2x" }, { 3.0f, "3x" } };
        for (const auto &s : scales) {
            if (ImGui::MenuItem(s.label, nullptr, shell.ui_scale == s.scale)) {
                shell.ui_scale_request = s.scale;
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // Independent of the focused tile and of a project (CAT-01).
    if (ImGui::MenuItem("Widget Catalog")) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::catalog);
    }
}

void rv_editor_menu_project(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    if (ImGui::MenuItem("Project Settings")) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::project);
    }
    ImGui::Separator();
    if (rv_editor_menu_item("Build", "Ctrl+B", rv_editor_app_why_not_build(app))) {
        rv_editor_app_build(app);
    }
    const char *why_not_cancel = app.build.state() == rv_editor_build_state::building ? nullptr : "No build to cancel";
    if (rv_editor_menu_item("Cancel Build", nullptr, why_not_cancel)) {
        app.build.cancel();
    }
}

void rv_editor_menu_run(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    const bool paused = app.session.state() == rv_editor_run_state::paused;
    if (rv_editor_menu_item(paused ? "Resume" : "Run", "F5", rv_editor_app_why_not_run(app))) {
        rv_editor_app_run(app);
    }
    if (rv_editor_menu_item("Run Last Successful Build", nullptr, rv_editor_app_why_not_run_last(app))) {
        rv_editor_app_run_last(app);
    }
    if (rv_editor_menu_item("Pause", "F6", rv_editor_app_why_not_pause(app))) {
        rv_editor_app_pause(app);
    }
    if (rv_editor_menu_item("Step Frame", "F7", rv_editor_app_why_not_step(app))) {
        rv_editor_app_step(app);
    }
    if (rv_editor_menu_item("Stop", "Shift+F5", rv_editor_app_why_not_stop(app))) {
        rv_editor_app_stop(app);
    }
    const char *why_not_reload = rv_editor_app_can_reload(app)
        ? rv_editor_app_why_not_reload(app)
        : "The running disc cannot reload: it runs from an image, or has no entry script";
    if (rv_editor_menu_item("Reload Entry Script", "F8", why_not_reload)) {
        rv_editor_app_reload(app);
    }
    ImGui::Separator();
    if (rv_editor_menu_item("Force Stop", nullptr, app.session.live() ? nullptr : "No runtime is running")) {
        app.session.force_stop(app.log);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Run Configuration...")) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::run_config);
    }
}

void rv_editor_menu_window(rv_editor_shell &shell)
{
    if (ImGui::MenuItem("Terminal")) {
        rv_editor_shell_focus_terminal(shell);
    }
    if (ImGui::MenuItem("Focus Next Pane", "Ctrl+F6")) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (ImGui::MenuItem("Focus Previous Pane", "Ctrl+Shift+F6")) {
        rv_editor_shell_focus_next(shell, true);
    }
    ImGui::Separator();
    // Every kind of tile, beside the focused one (LAY-01).
    if (ImGui::BeginMenu("New Tile")) {
        for (uint32_t k = 1; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
            const auto kind = static_cast<rv_editor_pane_kind>(k);
            if (ImGui::MenuItem(rv_editor_pane_title(kind))) {
                rv_editor_shell_new_tile(shell, kind);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // The same four the bar's right end switches between; each keeps its tiles.
    if (ImGui::BeginMenu("Reference Layouts")) {
        for (const rv_editor_layout_preset preset : rv_editor_switcher) {
            if (ImGui::MenuItem(rv_editor_layout_preset_name(preset), nullptr, rv_editor_shell_chosen(shell, preset))) {
                rv_editor_shell_choose(shell, preset);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset Layout")) {
        rv_editor_shell_reset_layout(shell, shell.active);
    }
}

void rv_editor_menu_help(rv_editor_shell &shell)
{
    if (ImGui::MenuItem("Keyboard Shortcuts")) {
        shell.help_open = true;
    }
}

} // namespace

void rv_editor_shell_menu(rv_editor_shell &shell)
{
    rv_editor_menu_style_push();
    if (!ImGui::BeginMainMenuBar()) {
        rv_editor_menu_style_pop();
        return;
    }
    constexpr struct
    {
        const char *label;
        void (*draw)(rv_editor_shell &);
    } menus[] = { { "File", rv_editor_menu_file }, { "Edit", rv_editor_menu_edit }, { "View", rv_editor_menu_view },
        { "Project", rv_editor_menu_project }, { "Scene", rv_editor_menu_scene }, { "Run", rv_editor_menu_run },
        { "Window", rv_editor_menu_window },
        { "Help", rv_editor_menu_help } };
    for (const auto &m : menus) {
        if (rv_editor_menu_begin(m.label)) {
            m.draw(shell);
            ImGui::EndMenu();
        }
    }
    // The layout switch at the bar's right end, as in the references: one place, always.
    float names = 0.0f;
    for (const rv_editor_layout_preset preset : rv_editor_switcher) {
        names += ImGui::CalcTextSize(rv_editor_layout_preset_name(preset)).x + 2.0f * ImGui::GetStyle().ItemSpacing.x;
    }
    const float at = ImGui::GetWindowWidth() - names - ImGui::GetStyle().WindowPadding.x;
    if (at > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(at);
        for (const rv_editor_layout_preset preset : rv_editor_switcher) {
            if (ImGui::MenuItem(rv_editor_layout_preset_name(preset), nullptr, rv_editor_shell_chosen(shell, preset))) {
                rv_editor_shell_choose(shell, preset);
            }
        }
    }
    ImGui::EndMainMenuBar();
    rv_editor_menu_style_pop();
}

void rv_editor_shell_shortcuts(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    // A text field keeps its own keys; F-keys are never text.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B)) {
        rv_editor_app_build(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)) {
        app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F5)) {
        rv_editor_app_run(app);
    }
    // Tab belongs to the code editor and the terminal; these move between tiles.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6)) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F6)) {
        rv_editor_shell_focus_next(shell, true);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F5)) {
        rv_editor_app_stop(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F6)) {
        rv_editor_app_pause(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F7)) {
        rv_editor_app_step(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F8)) {
        rv_editor_app_reload(app);
    }
}

void rv_editor_shell_new_file(rv_editor_shell &shell)
{
    rv_editor_menu_code_keys(shell, "<Cmd>enew<CR>");
}

void rv_editor_shell_toolbar(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    const float side = ImGui::GetFrameHeight();
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? "No code tile yet: Window > New Tile > Code" : nullptr;
    const rv_editor_state code_state{ rv_editor_look::live, no_code };
    if (rv_editor_image_button("##new", "new-file", "New File", theme, code_state)) {
        rv_editor_menu_code_keys(shell, "<Cmd>enew<CR>");
    }
    ImGui::SameLine();
    if (rv_editor_image_button("##open", "open", "Open Directory...", theme)) {
        rv_editor_shell_open_folder(shell);
    }
    ImGui::SameLine();
    // In Scene with a scene open, Save, Undo and Redo are the scene's (SCN-03); elsewhere the code tile's.
    rv_editor_scene_doc *scene = shell.active == rv_editor_layout_preset::scene ? shell.app.scene.get() : nullptr;
    if (scene != nullptr) {
        const char *why_not_save = !scene->scene.read_only.empty() ? "The scene is read-only"
            : !scene->dirty                                        ? "Nothing to save"
                                                                   : nullptr;
        if (rv_editor_image_button("##save", "save", "Save Scene", theme, { rv_editor_look::live, why_not_save })) {
            std::string error;
            if (!rv_editor_app_scene_save(shell.app, error)) {
                shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not saved: " + error);
            }
        }
    } else if (rv_editor_image_button("##save", "save", "Save (Ctrl+S)", theme, code_state)) {
        rv_editor_menu_code_keys(shell, "<C-s>");
    }
    // A gap between the file actions and the edit actions.
    ImGui::SameLine(0.0f, side);
    if (scene != nullptr) {
        if (rv_editor_image_button("##undo", "undo", "Undo Scene Edit (Ctrl+Z in a scene tile)", theme,
                { rv_editor_look::live, scene->undo.empty() ? "Nothing to undo" : nullptr })) {
            rv_editor_scene_undo(*scene);
        }
        ImGui::SameLine();
        if (rv_editor_image_button("##redo", "redo", "Redo Scene Edit (Ctrl+Shift+Z in a scene tile)", theme,
                { rv_editor_look::live, scene->redo.empty() ? "Nothing to redo" : nullptr })) {
            rv_editor_scene_redo(*scene);
        }
        return;
    }
    if (rv_editor_image_button("##undo", "undo", "Undo (Ctrl+Z)", theme, code_state)) {
        rv_editor_menu_code_keys(shell, "<C-z>");
    }
    ImGui::SameLine();
    if (rv_editor_image_button("##redo", "redo", "Redo (Ctrl+Shift+Z)", theme, code_state)) {
        rv_editor_menu_code_keys(shell, "<C-S-z>");
    }
}

void rv_editor_shell_terminal_dialog(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.closing_terminal == rv_editor_tile_none) {
        return;
    }
    const char *title = "End the shell?";
    ImGui::OpenPopup(title);
    if (!rv_editor_dialog_begin(title, theme)) {
        return;
    }
    ImGui::TextUnformatted("The shell in this terminal is still running. Closing the tile ends it");
    ImGui::TextUnformatted("and everything started in it. Another tab keeps it running instead.");
    if (rv_editor_button("End Shell", theme)) {
        rv_editor_tile_remove(shell.ws.layout, shell.closing_terminal);
        shell.closing_terminal = rv_editor_tile_none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (rv_editor_button("Keep", theme) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        shell.closing_terminal = rv_editor_tile_none;
        ImGui::CloseCurrentPopup();
    }
    rv_editor_dialog_end();
}

void rv_editor_shell_help(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.help_open) {
        ImGui::OpenPopup("Keyboard Shortcuts");
        shell.help_open = false;
    }
    if (!rv_editor_dialog_begin("Keyboard Shortcuts", theme)) {
        return;
    }
    constexpr const char *keys[][2] = { { "Save / Save All", "Ctrl+S / Ctrl+Shift+S" },
        { "Undo / Redo", "Ctrl+Z / Ctrl+Shift+Z" }, { "Find in Project", "Ctrl+Shift+F" }, { "Build", "Ctrl+B" },
        { "Run / Resume", "F5" }, { "Pause", "F6" }, { "Step Frame", "F7" }, { "Reload", "F8" }, { "Stop", "Shift+F5" },
        { "Release Game input", "Shift+Esc" },
        { "Vim mode in a code tile", "F2" },
        { "Focus Next / Previous Pane", "Ctrl+F6 / Ctrl+Shift+F6" } };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto &k : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k[0]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k[1]);
        }
        ImGui::EndTable();
    }
    if (rv_editor_button("Close", theme) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    rv_editor_dialog_end();
}

} // namespace rv_editor
