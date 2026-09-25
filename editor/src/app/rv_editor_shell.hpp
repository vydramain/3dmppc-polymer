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
    // Unsaved buffers stand between the user and leaving: the window closing or
    // another project opening. Nothing leaves until they are saved or discarded.
    enum class rv_editor_leave
    {
        none,
        quit,
        open,
    };
    rv_editor_leave leaving = rv_editor_leave::none;
    std::filesystem::path leaving_to; // the project Open waits to open
    bool quit_now = false;            // answered: the window closes
    // A save is out to nvim; what came back failed, file by file; and a save
    // that succeeded whole, for the next frame to act on.
    bool saving = false;
    std::vector<rv_editor_nvim_saved> save_failed;
    bool save_done = false;
    // Save As: the buffer being named (0: none), the path typed, why it failed.
    int64_t save_as_buffer = 0;
    char save_as_path[512] = {};
    std::string save_as_error;
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

// Opens a picked project, or asks first when buffers are unsaved (the question
// is rv_editor_shell_dialogs').
void rv_editor_shell_request_open(rv_editor_shell &shell, const std::filesystem::path &path);
// What waits for a finished save: the tile closes, the window closes, the
// project opens. Once a frame, from rv_editor_shell_update.
void rv_editor_shell_after_save(rv_editor_shell &shell);
// File > Save As for the buffer in the focused code tile.
void rv_editor_shell_save_as_start(rv_editor_shell &shell);

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
