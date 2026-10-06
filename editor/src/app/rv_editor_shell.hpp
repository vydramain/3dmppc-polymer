#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include <SDL3/SDL.h>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_browser.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

// The page the window without a project shows beside its Toolchest.
enum class rv_editor_start_page
{
    recent,
    new_project,
    open_project,
    settings,
    help,
};

// New Project's form as typed, kept while other pages are shown; Reset clears it.
struct rv_editor_start
{
    char name[128] = {};
    char id[64] = {};
    char dir[512] = {};
    bool id_edited = false; // the id no longer follows the name
    size_t template_index = 0;
    std::string error;
    bool browsing = false; // Directory's browser is open beside the form
    rv_editor_browser browser;
};

// One editor window: its tiles, its models, and the commands that reach them
// from the menus and the keyboard.
struct rv_editor_shell
{
    // The layouts Code, Scene, Debug and Burn, each a tile tree of its own over the
    // one pane registry in `ws`: choosing one swaps trees only, so no pane, shell,
    // nvim window or process goes with it. `ws.layout` is the chosen one's tree;
    // `trees` keeps the others (the chosen one's slot is stale).
    rv_editor_workspace ws;
    std::array<rv_editor_layout, 4> trees;
    // By slot: a starting tree whose Game tile keeps the screen's proportions until
    // a splitter is dragged; the tries left to settle, and the area at the last one.
    std::array<bool, 4> game_fit{};
    // By slot: true from a reset/fresh preset until the first splitter drag, whether
    // or not the preset also fits the Game (Debug does not, but its strips still do).
    std::array<bool, 4> layout_untouched{};
    int game_fit_tries = 8;
    rv_editor_size game_fit_last{ 0, 0 };
    rv_editor_layout_preset active = rv_editor_layout_preset::code;
    rv_editor_app app;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr; // the Game tile's texture lives on it

    // A code tile with unsaved changes waiting for Save, Discard or Cancel.
    rv_editor_pane_id closing = rv_editor_tile_none;
    // Unsaved buffers stand between the user and leaving: the window closing or
    // another project opening. Nothing leaves until they are saved or discarded.
    enum class rv_editor_leave
    {
        none,
        quit,
        open,
        build, // Build met unsaved files
        run,
        build_restart, // Build and Restart met unsaved files
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
    rv_editor_pane_id save_as_pane = rv_editor_tile_none; // the code tile Save As shows in
    // The window has the keyboard, as SDL's focus events say.
    bool window_focused = true;
    // The file Files last followed, so a selection the user makes there stays
    // until the document in front changes.
    std::string revealed;
    // The code tile used last: what Files opens goes there while Files has the focus.
    rv_editor_pane_id last_code = rv_editor_tile_none;
    // Help's filter as typed, kept while other pages or tabs are in front.
    char help_filter[64] = {};
    rv_editor_start start;
    rv_editor_start_page start_page = rv_editor_start_page::recent;
    std::filesystem::path start_selected; // the Project Catalog's selected project; kept while New Project resets start
    // The project Remove from Recent took off the list last, and its place, for Undo.
    std::filesystem::path recent_removed;
    size_t recent_removed_at = 0;
    // Open Project's browser, on the start page or in a tab; empty purpose: not started.
    rv_editor_browser open_browser;
    // A terminal tile with a live shell waiting for End Shell or Keep.
    rv_editor_pane_id closing_terminal = rv_editor_tile_none;
    // The UI scale shown, and one View > UI Scale asked for, applied between frames (0: none).
    float ui_scale = 1.0f;
    float ui_scale_request = 0.0f;
    // Settings' draft: read from the tools when the page first shows and after
    // Apply or Revert, kept while other pages or tabs are in front.
    bool settings_loaded = false;
    char settings_paths[4][512] = {};
    std::string settings_error;
    // What Check found for a field as it stood; dropped when the field changes.
    std::array<std::optional<rv_editor_tool>, 4> settings_checks;
    int settings_browse = -1; // the field the browser is choosing for; -1: none
    rv_editor_browser settings_browser;
    // What each strip of controls drew last frame: its minimum in the tree.
    std::map<rv_editor_pane_id, rv_editor_size> strips;
};

// Every tree a window keeps, as the switcher and Window > Reference Layouts offer them.
inline constexpr rv_editor_layout_preset rv_editor_workspaces[] = { rv_editor_layout_preset::code,
    rv_editor_layout_preset::scene, rv_editor_layout_preset::debug, rv_editor_layout_preset::burn };

// Shows layout `to` as the user left it. Nothing else changes.
void rv_editor_shell_switch(rv_editor_shell &shell, rv_editor_layout_preset to);

// Layout > Reset Layout: the chosen layout's tree becomes its starting one; the
// others keep theirs.
void rv_editor_shell_reset_layout(rv_editor_shell &shell, rv_editor_layout_preset preset);

// Once a frame: a starting tree's Game tile takes the proportions of the disc's
// screen, by the split under or over it, else beside it, until the user drags a
// splitter; a strip of controls keeps its height. Saved trees are left as they are.
void rv_editor_shell_fit_game(rv_editor_shell &shell);

// Once a frame: switches layout if Assets asked for one (see rv_editor_shell_frame_start).
void rv_editor_shell_take_layout_request(rv_editor_shell &shell);

// Once a frame, before drawing: takes the layout request, then fits the Game.
void rv_editor_shell_frame_start(rv_editor_shell &shell);

// Each layout's saved tree from `path`-<name> (read from the file an earlier
// editor kept it in while that is missing), or its starting one when there is
// none or it cannot be read; then shows `active` ("code", "scene", "debug",
// "burn"; anything else is Code).
void rv_editor_shell_load_layouts(rv_editor_shell &shell, const std::filesystem::path &path, const std::string &active);

// Writes every layout's tree to `path`-<name>. RV_OK or the code of the first that failed; the others
// are still written.
int rv_editor_shell_save_layouts(const rv_editor_shell &shell, const std::filesystem::path &path, std::string &error);

// The active workspace's name as the view file keeps it.
const char *rv_editor_shell_workspace_key(const rv_editor_shell &shell);

// True while any workspace's tree holds `pane`, shown or not: its nvim window
// and its shell stay.
bool rv_editor_shell_pane_kept(const rv_editor_shell &shell, rv_editor_pane_id pane);

// The menu bar (rv_editor_shell_menu.cpp). Runs before the workspace is drawn, so
// a change here never lands under a reference the drawing holds.
void rv_editor_shell_menu(rv_editor_shell &shell);

// The shortcuts of the spec's section 13 that have a command today.
void rv_editor_shell_shortcuts(rv_editor_shell &shell);

// Help as a page or a tab: the keyboard shortcuts, filtered by what is typed.
void rv_editor_page_help(rv_editor_shell &shell, const rv_editor_theme &theme);

// The embedded user manual, as a page and a tab.
void rv_editor_page_manual(rv_editor_shell &shell, const rv_editor_theme &theme);

// A menu item that is disabled with its reason shown on hover.
bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not);
// The main menu's Scene (editor/src/app/rv_editor_shell_menu_scene.cpp).
void rv_editor_menu_scene(rv_editor_shell &shell);
// Save Scene: the menu item and Ctrl+S; does nothing when the item would be disabled.
void rv_editor_shell_scene_save(rv_editor_shell &shell);
// Opens the New Scene area with today's free name (Scene > New Scene, the
// empty Scene pane's Create Scene).
void rv_editor_shell_new_scene_request(const rv_editor_app &app);

// A page the Toolchest or a menu asks for: beside the Toolchest without a project,
// a tab with one (Recent Projects and New Project are start pages only).
void rv_editor_shell_page(rv_editor_shell &shell, rv_editor_start_page page);
// Open Project...: rv_editor_shell_page for its page.
void rv_editor_shell_open_project(rv_editor_shell &shell);

// A pane of `kind` in front: the one the tree shows, or a new tab of the focused
// tile, or of the first tile when none is focused; `roomy`: of the biggest tile.
void rv_editor_shell_show_pane(rv_editor_shell &shell, rv_editor_pane_kind kind, bool roomy = false);

// Status bar's four texts: project name, tools, build and runtime states.
struct rv_editor_status_text {
    std::string where;
    std::string tools;
    std::string build;
    std::string runtime;
    int shown; // 2 without open project, 4 with one
};

// Builds the bar's texts from the app's state.
rv_editor_status_text rv_editor_status_text_make(const rv_editor_app &app);

// Draws the status bar's four texts with the theme.
void rv_editor_shell_status(const rv_editor_shell &shell, const rv_editor_theme &theme);

// The window's content while no project is open (rv_editor_shell_start.cpp).
void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme);
// The start screen's pages that are drawn in a tab too: New Project
// (rv_editor_shell_start_new.cpp) and Open Project (rv_editor_shell_start.cpp).
void rv_editor_page_new_project(rv_editor_shell &shell, const rv_editor_theme &theme);
void rv_editor_page_open_project(rv_editor_shell &shell, const rv_editor_theme &theme);

// Window > Terminal: the terminal in front, with the keyboard.
void rv_editor_shell_focus_terminal(rv_editor_shell &shell);
// Window > Focus Next / Previous Pane: the next tile in reading order takes the keyboard.
void rv_editor_shell_focus_next(rv_editor_shell &shell, bool back);

// A live shell's tile closes only after this question; drawn after the workspace.
void rv_editor_shell_ask_terminal(rv_editor_shell &shell, const rv_editor_theme &theme);

// A new tile of `kind` beside the focused tile (or the first), half and half.
void rv_editor_shell_new_tile(rv_editor_shell &shell, rv_editor_pane_kind kind);

// A new Untitled buffer in the code tile used last.
void rv_editor_shell_new_file(rv_editor_shell &shell);

// File > Save All: every modified buffer.
void rv_editor_shell_save_all(rv_editor_shell &shell);

// Once a frame before drawing: opens what a dialog picked and updates the models.
void rv_editor_shell_update(rv_editor_shell &shell);

// Opens a picked project, or asks first when buffers are unsaved (the question
// is Review Changes).
void rv_editor_shell_request_open(rv_editor_shell &shell, const std::filesystem::path &path);
// What waits for a finished save: the tile closes, the window closes, the
// project opens. Once a frame, from rv_editor_shell_update.
void rv_editor_shell_after_save(rv_editor_shell &shell);
// A buffer's file as the project sees it: relative to the root when inside it,
// "Untitled" without a name.
std::string rv_editor_shell_buffer_label(const rv_editor_app &app, const std::string &name);

// File > Save As for the buffer in the focused code tile.
void rv_editor_shell_save_as_start(rv_editor_shell &shell);

// rv_editor_pane_close_fn for the window's workspace: a code tile whose buffer
// is modified and shown nowhere else asks first.
bool rv_editor_shell_close_pane(void *context, rv_editor_pane_id pane);

// True when the window may close now; otherwise it asks about the modified
// buffers first and closes once answered.
bool rv_editor_shell_may_quit(rv_editor_shell &shell);

// The questions about unsaved files, none of them over the window: inside the
// code tile that is closing, Save As inside the tile it was chosen in, and
// Review Changes as a tab while quitting, opening, building or running waits.
void rv_editor_shell_ask_close(rv_editor_shell &shell, const rv_editor_theme &theme);
void rv_editor_shell_ask_save_as(rv_editor_shell &shell, const rv_editor_theme &theme);
void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme);
// Once a frame: Review Changes is a tab while something waits on unsaved files and
// goes when nothing does; Save As whose code tile has gone is dropped.
void rv_editor_shell_review_tab(rv_editor_shell &shell);
// File > Settings: the tool paths of settings.toml.
void rv_editor_page_settings(rv_editor_shell &shell, const rv_editor_theme &theme);

// Once a frame after drawing: sends the Game's keys while a drawn Game tile
// holds the keyboard of a focused window and a console runs, and every key up
// the moment any of that stops.
void rv_editor_shell_game_input(rv_editor_shell &shell);

// rv_editor_pane_draw_fn for the window's workspace; `context` is the shell.
void rv_editor_shell_pane(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme);

// shell.ws.minimums[pane]: `kind`'s minimum (rv_editor_shell_minimums.cpp), the
// Game's own mode row and status line (`game_need`, measured by the pane itself
// rather than the frame at 1x) when larger, a strip's measured height when that
// is larger still; unset when every source gives none.
void rv_editor_shell_set_minimum(rv_editor_shell &shell, rv_editor_pane_id pane, rv_editor_pane_kind kind,
    rv_editor_size game_need);

// True if UI scale fits on the window's display in 1280*scale x 720*scale pixels.
bool rv_editor_shell_scale_fits(SDL_Window *window, float scale);

// Display size in pixels.
struct rv_editor_display_size {
    int w_pixels = 0;
    int h_pixels = 0;
};

// Get display size in pixels for the given window's display.
rv_editor_display_size rv_editor_get_display_size(SDL_Window *window);

// Show error message window when display is too small; prints to stderr and exits via SDL_Quit.
void rv_editor_show_too_small_error(SDL_Window *&window, SDL_Renderer *&renderer,
    const rv_editor_display_size &display_size);

} // namespace rv_editor
