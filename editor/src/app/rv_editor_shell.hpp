#pragma once

#include <filesystem>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

// One editor window: its tiles, its models, and the commands that reach them
// from the menus and the keyboard.
struct rv_editor_shell
{
    rv_editor_workspace ws;
    rv_editor_app app;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr; // the Game tile's texture lives on it

    // Paths SDL's file dialogs returned, possibly from another thread; opened
    // on the next frame.
    std::mutex picked_mutex;
    std::vector<std::filesystem::path> picked;

    // A code tile with unsaved changes waiting for Save, Discard or Cancel.
    rv_editor_pane_id closing = rv_editor_tile_none;
    bool quit_asked = false; // the same question for the whole window
    bool quit_now = false;   // answered: the window closes
    // The window has the keyboard, as SDL's focus events say.
    bool window_focused = true;
    // The file Files last followed, so a selection the user makes there stays
    // until the document in front changes.
    std::string revealed;
};

rv_editor_workspace rv_editor_workspace_preset(rv_editor_layout_preset preset);

// The saved layout, or Code when there is none or it cannot be read (LAY-06).
rv_editor_workspace rv_editor_workspace_load(const std::filesystem::path &path);

// File, Run, Layout and Window menus. Runs before the workspace is drawn, so a
// change here never lands under a reference the drawing holds.
void rv_editor_shell_menu(rv_editor_shell &shell);

// The shortcuts of section 12 of the requirements that have a command today.
void rv_editor_shell_shortcuts(rv_editor_shell &shell);

// Once a frame before drawing: opens what a dialog picked and updates the models.
void rv_editor_shell_update(rv_editor_shell &shell);

// rv_editor_pane_close_fn for the window's workspace: a code tile whose buffer
// is modified and shown nowhere else asks first (TXT-07).
bool rv_editor_shell_close_pane(void *context, rv_editor_pane_id pane);

// True when the window may close now; otherwise it asks about the modified
// buffers first and closes once answered (TXT-07, AC-22).
bool rv_editor_shell_may_quit(rv_editor_shell &shell);

// The shell's own dialogs, drawn after the workspace.
void rv_editor_shell_dialogs(rv_editor_shell &shell, const rv_editor_theme &theme);

// Once a frame after drawing: sends the Game's keys while a drawn Game tile
// holds the keyboard of a focused window and a console runs, and every key up
// the moment any of that stops (GAM-04).
void rv_editor_shell_game_input(rv_editor_shell &shell);

// rv_editor_pane_draw_fn for the window's workspace; `context` is the shell.
void rv_editor_shell_pane(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme);

} // namespace rv_editor
