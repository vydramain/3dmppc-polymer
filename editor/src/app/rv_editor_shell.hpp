#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_browser.hpp"
#include "app/rv_editor_scene_tabs.hpp"
#include "app/rv_editor_shell_review.hpp"
#include "app/rv_editor_shell_shortcut_settings.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

// Frame pacing as Settings shows it: vsync, and the frame rate limit that applies while it is off.
struct rv_editor_pace {
    bool vsync = true;
    int fps_limit = fps_limit_default;
};

// The page the window without a project shows beside its Toolchest.
enum class rv_editor_start_page {
    recent,
    new_project,
    open_project,
    settings,
    help,
    manual,
};

enum class rv_editor_manual_step {
    none,
    open_picker,
    pick_project,
    new_tile,
    close_tile,
    build_project,
    run_project,
    complete,
};

struct rv_editor_manual_guide {
    rv_editor_manual_step step = rv_editor_manual_step::none;
    rv_editor_pane_id pane = rv_editor_tile_none;
    rv_editor_layout_preset tile_layout = rv_editor_layout_preset::code;
    uint64_t build_first_seq = 0;
    uint32_t build_number = 0;
    uint32_t session = 0;
};

// New Project's form as typed, kept while other pages are shown; Reset clears it.
struct rv_editor_start {
    char name[identifier_field_size] = {};
    char id[short_text_field_size] = {};
    char dir[filesystem_path_field_size] = {};
    bool id_edited = false; // the id no longer follows the name
    size_t template_index = 0;
    std::string error;
    bool browsing = false; // Directory's browser is open beside the form
    rv_editor_browser browser;
};

// Settings: number of tool paths checked in the Toolchain (baker, burner, console, player)
constexpr size_t settings_tools_count = 4;

// Workspaces: number of layouts (Code, Scene, Debug, Burn) in rv_editor_workspaces.
constexpr size_t editor_workspace_slots_count = 4;

// The window's UI state, owned by the shell: panes read it and change it only by
// queuing an rv_editor_ui_request; the fields move here out of rv_editor_app one by one.
struct rv_editor_ui {
    // A code tile had the keyboard this frame: ImGui's own keyboard navigation
    // stays off the next one, so arrows and Tab reach nvim.
    bool text_focus = false;
    // How the Game tile scales the frame; kept in the view file.
    rv_editor_game_scale game_scale = rv_editor_game_scale::fit;
    // The Game tile has the keyboard; and whether one was drawn this frame: a
    // Game tile closed or behind another tab holds no keys.
    bool game_captured = false;
    bool game_drawn = false;
    // The Game tile's own minimum: its mode row and status line (the picture scales
    // below 1x to fit); 0 x 0 before the first frame.
    rv_editor_size game_need{ 0, 0 };
    // The picture area the Game tile gives a running game, measured at every draw.
    rv_editor_size game_area{ 0, 0 };
    // The entry selected in Files; empty: none.
    std::filesystem::path files_selected;
    // The Scene viewport's view and the Transform tools' choice, shared by the viewport and
    // the Transform Toolchest.
    rv_editor_scene_camera scene_camera;
    // The Scene tile's tabs: the scene is tab 0 (front 0), the tabs open from Assets follow.
    rv_editor_scene_tabs scene_tabs;
};

enum class rv_editor_ui_request_kind {
    take_text_focus,
    capture_keyboard,
    release_keyboard,
    set_game_scale,
    game_drawn,
    game_measured,
    select_file,
    set_scene_camera,
    open_scene_tab,
    front_scene_tab,
    close_scene_tab,
    count, // number of kinds; the apply table has one row per kind
};

// One change of rv_editor_ui, applied by rv_editor_shell_ui_apply. Each kind reads
// only the fields it names: focus, scale, need and area, path, camera, scene tab, or
// scene tab index: the front index (0: the scene) for front_scene_tab, the 1-based tab for close_scene_tab.
struct rv_editor_ui_request {
    rv_editor_ui_request_kind kind;
    rv_editor_game_scale scale = rv_editor_game_scale::fit;
    rv_editor_size need{ 0, 0 };
    rv_editor_size area{ 0, 0 };
    bool focus = false;
    std::filesystem::path path = {};
    rv_editor_scene_camera camera = {};
    rv_editor_scene_tab scene_tab = {};
    int scene_tab_index = 0;
};

// What a pane gets: panes read the state through it and change it only by queuing requests.
struct rv_editor_ui_port {
    const rv_editor_ui &state;
    std::vector<rv_editor_ui_request> &requests;
};

// The focused document when a top-level menu opened.
struct rv_editor_menu_document {
    uint32_t menu = 0;
    uint64_t generation = 0;
    rv_editor_pane_id pane = rv_editor_tile_none;
    rv_editor_pane_kind kind = rv_editor_pane_kind::empty;
    int64_t buffer = 0;
    std::filesystem::path scene_path;
    bool history_requested = false;
    std::optional<rv_editor_nvim_history> history;
};

// One editor window: its tiles, its models, and the commands that reach them
// from the menus and the keyboard.
struct rv_editor_shell {
    // The layouts Code, Scene, Debug and Burn, each a tile tree of its own over the
    // one pane registry in `ws`: choosing one swaps trees only, so no pane, shell,
    // nvim window or process goes with it. `ws.layout` is the chosen one's tree;
    // `trees` keeps the others (the chosen one's slot is stale).
    rv_editor_workspace ws;
    std::array<rv_editor_layout, editor_workspace_slots_count> trees;
    // By slot: a starting tree whose Game tile keeps the screen's proportions until
    // a splitter is dragged.
    std::array<bool, editor_workspace_slots_count> game_fit{};
    // By slot: true from a reset/fresh preset until the first splitter drag; its
    // strips are fit to their buttons meanwhile, and the Game while game_fit holds.
    std::array<bool, editor_workspace_slots_count> layout_untouched{};
    rv_editor_layout_preset active = rv_editor_layout_preset::code;
    rv_editor_app app;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr; // the Game tile's texture lives on it
    // A refused pad is retried every frame, so its warning is logged once per run of refusals.
    // Cleared by a frame with nothing to send (pad_pending false) and by the next pad that is sent.
    bool pad_refusal_reported = false;
    // The session number game_input saw last frame: a new console ends the Game's keyboard capture.
    uint32_t game_session = 0;

    // A code tile with unsaved changes waiting for Save, Discard or Cancel.
    rv_editor_pane_id closing = rv_editor_tile_none;
    using rv_editor_leave = rv_editor_review_action;
    rv_editor_leave leaving = rv_editor_leave::none;
    rv_editor_review review;
    bool quit_now = false; // answered: the window closes
    // A save is out to nvim; what came back failed, file by file; and a save
    // that succeeded whole, for the next frame to act on.
    bool saving = false;
    std::vector<rv_editor_nvim_saved> save_failed;
    bool save_done = false;
    // Save As: the buffer being named (0: none), the path typed, why it failed.
    int64_t save_as_buffer = 0;
    char save_as_path[filesystem_path_field_size] = {};
    std::string save_as_error;
    rv_editor_pane_id save_as_pane = rv_editor_tile_none; // the code tile Save As shows in
    // The window has the keyboard, as SDL's focus events say.
    bool window_focused = true;
    // The file Files last followed, so a selection the user makes there stays
    // until the document in front changes.
    std::string revealed;
    // The code tile used last: what Files opens goes there while Files has the focus.
    rv_editor_pane_id last_code = rv_editor_tile_none;
    rv_editor_menu_document menu_document;
    // Help's filter as typed, kept while other pages or tabs are in front.
    char help_filter[short_text_field_size] = {};
    rv_editor_manual_guide manual;
    bool manual_reference = false;
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
    // The UI scale shown, and one Editor > UI Scale asked for, applied between frames (0: none).
    float ui_scale = 1.0f;
    float ui_scale_request = 0.0f;
    // The main font size shown, and one menu request to apply between frames.
    rv_editor_main_size main_size = rv_editor_main_size::normal;
    std::optional<rv_editor_main_size> main_size_request;
    // The window minimum width the last rv_editor_shell_window_min_changed returned; 0 before the first call.
    int window_min_last = 0;
    // The pacing in use, and one Settings change asked for, applied between frames (empty: none).
    rv_editor_pace pace;
    bool vsync_active = false; // vsync as SDL reports it on the renderer, read after each rv_editor_shell_pace_apply
    std::optional<rv_editor_pace> pace_request;
    // Settings' draft: read from the tools when the page first shows and after
    // Apply or Revert, kept while other pages or tabs are in front.
    bool settings_loaded = false;
    char settings_paths[settings_tools_count][filesystem_path_field_size] = {};
    std::string settings_error;
    // Apply was pressed; the frame after saves and looks the tools up (rv_editor_shell_settings_apply).
    bool settings_apply_request = false;
    // What Check found for a field as it stood; dropped when the field changes.
    std::array<std::optional<rv_editor_tool>, settings_tools_count> settings_checks;
    int settings_browse = -1; // the field the browser is choosing for; -1: none
    rv_editor_browser settings_browser;
    // The UI state, and the requests the panes queued for rv_editor_shell_ui_apply.
    rv_editor_ui ui;
    std::vector<rv_editor_ui_request> ui_requests;
    std::optional<rv_editor_shortcut_chords> shortcuts;
    std::optional<rv_editor_shortcut_chords> settings_shortcuts;
    std::optional<rv_editor_shortcut_action> settings_shortcut_capture;
    std::string settings_shortcut_error;
};

// Every tree a window keeps, as the switcher and Editor > Layout offer them.
inline constexpr rv_editor_layout_preset rv_editor_workspaces[] = { rv_editor_layout_preset::code,
    rv_editor_layout_preset::scene,
    rv_editor_layout_preset::debug,
    rv_editor_layout_preset::burn };

// Whether the starting tree of `preset` sizes its Game tile to the picture.
bool rv_editor_preset_fits_game(rv_editor_layout_preset preset);

// Shows layout `to` as the user left it. Nothing else changes.
void rv_editor_shell_switch(rv_editor_shell &shell, rv_editor_layout_preset to);

// Editor > Reset Current Layout: the active layout's tree becomes its starting one; the
// others keep theirs.
void rv_editor_shell_reset_layout(rv_editor_shell &shell, rv_editor_layout_preset preset);

// Once a frame: a starting tree's Game tile takes the proportions of the disc's
// screen, by the split under or over it, else beside it, until the user drags a
// splitter; a strip of controls keeps its height. Saved trees are left as they are.
void rv_editor_shell_fit_game(rv_editor_shell &shell);

// Sets `ws.layout` so the Game's picture area (`area` now, in `ws.rects`) is the largest whole number of the screen's
// aspect steps the other tiles' minimums let by, but not taller than `target_h` px unless the Game's own minimum is
// (rv_editor_shell_game_fit.cpp); `strip_like` tells the tiles that keep their pixels.
void rv_editor_fit_game_tree(rv_editor_workspace &ws,
    rv_editor_pane_id game,
    rv_editor_size area,
    int32_t screen_w,
    int32_t screen_h,
    int32_t target_h,
    const std::function<bool(uint32_t)> &strip_like);

// Once a frame: switches layout if Assets asked for one (see rv_editor_shell_frame_start).
void rv_editor_shell_take_layout_request(rv_editor_shell &shell);

// Once a frame, before drawing: takes the layout request, then fits the Game.
void rv_editor_shell_frame_start(rv_editor_shell &shell);

// Once a frame after the panes drew: applies the queued UI requests in order, then clears them (rv_editor_shell_ui.cpp).
void rv_editor_shell_ui_apply(rv_editor_shell &shell);

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
void rv_editor_shell_menu(rv_editor_shell &shell, const rv_editor_theme &theme = rv_editor_theme_olive);
void rv_editor_menu_document_open(rv_editor_shell &shell, uint32_t menu);
void rv_editor_menu_document_save(rv_editor_shell &shell);
void rv_editor_menu_document_history(rv_editor_shell &shell);

// The shortcuts of the spec's section 13 that have a command today.
void rv_editor_shell_shortcuts(rv_editor_shell &shell);
// A live session owns Run, which resumes it when paused.
bool rv_editor_shell_run_targets_candidate(const rv_editor_shell &shell);

// Help as a page or a tab: the keyboard shortcuts, filtered by what is typed.
void rv_editor_page_help(rv_editor_shell &shell, const rv_editor_theme &theme);

void rv_editor_page_about(const rv_editor_theme &theme);

// Guided workflows and the embedded reference, as a page and a tab.
void rv_editor_page_manual(rv_editor_shell &shell, const rv_editor_theme &theme);
void rv_editor_shell_manual_start(rv_editor_shell &shell, rv_editor_manual_step first);
void rv_editor_shell_manual_update(rv_editor_shell &shell);
float rv_editor_shell_manual_draw(rv_editor_shell &shell, const rv_editor_theme &theme, ImVec2 top, float width);

// Scene tab's New Scene and Open Scene commands (rv_editor_shell_menu_scene.cpp).
void rv_editor_scene_file_menu(rv_editor_app &app);
// Save Scene: the menu item and Ctrl+S; does nothing when the item would be disabled.
void rv_editor_shell_scene_save(rv_editor_shell &shell);
const char *rv_editor_scene_why_not_save(const rv_editor_app &app);
bool rv_editor_scene_document_pane(rv_editor_pane_kind kind);
// Opens the New Scene area with today's free name (the Scene tab menu or the
// empty Scene pane's Create Scene).
void rv_editor_shell_new_scene_request(const rv_editor_app &app);

// A page the Toolchest or a menu asks for: beside the Toolchest without a project,
// a tab with one (Recent Projects and New Project are start pages only).
void rv_editor_shell_page(rv_editor_shell &shell, rv_editor_start_page page);
// Open Project...: rv_editor_shell_page for its page.
void rv_editor_shell_open_project(rv_editor_shell &shell);
void rv_editor_shell_request_close_project(rv_editor_shell &shell);
const char *rv_editor_shell_why_not_project_switch(const rv_editor_shell &shell);

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

// "Tools: ready", "Tools: 7 missing", "Tools: 999+ missing" (a count over 999 is shown as 999+).
std::string rv_editor_status_tools_text(int missing);

// The longest text each segment can show: the project's name as it is, the others with their fields at their limits
// (Tools: 999+ missing; Build #999999 step 99/99: <16 chars>; Runtime: <longest state>, session 999999).
rv_editor_status_text rv_editor_status_text_widest(const rv_editor_app &app);

// Least width of the status bar for the app's project: widest texts, paddings and gaps; needs a frame (measures text).
float rv_editor_shell_status_min_width(const rv_editor_app &app, const rv_editor_theme &theme);

// Least editor window width in pixels: 1280 * scale, or the status bar's `status_min` (pixels) if that is more.
int rv_editor_window_min_width_for(float scale, float status_min);

// True when the window's least width (rv_editor_window_min_width_for of the status bar's minimum for the shell's current
// project) differs from the one the previous call returned, or on the first call; `min_out` gets it in pixels. Callable
// every frame, outside a frame too.
bool rv_editor_shell_window_min_changed(rv_editor_shell &shell, const rv_editor_theme &theme, int &min_out);

// Sets the SDL window's least size to the scaled layout minimum, with room for the status bar up to the display width,
// and enlarges the window when it is smaller. False when SDL refused.
bool rv_editor_window_min_sync(SDL_Window *window, rv_editor_shell &shell, const rv_editor_theme &theme);

// Draws the status bar's texts with the theme; a click on the Tools segment opens Settings (Toolchain list).
// `probe` gets what the bar drew.
void rv_editor_shell_status(rv_editor_shell &shell, const rv_editor_theme &theme, rv_editor_status_probe *probe = nullptr);

// The window's content while no project is open (rv_editor_shell_start.cpp).
void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme);
// The start screen's pages that are drawn in a tab too: New Project
// (rv_editor_shell_start_new.cpp) and Open Project (rv_editor_shell_start.cpp).
void rv_editor_page_new_project(rv_editor_shell &shell, const rv_editor_theme &theme);
// What the Directory field drew last, for tests.
const rv_editor_path_probe &rv_editor_page_new_project_dir_probe();
void rv_editor_page_open_project(rv_editor_shell &shell, const rv_editor_theme &theme);

// The Recent Projects page of the start screen (records with a status, the selected project's card) and the least tile
// it draws whole in every state.
void rv_editor_page_recent(rv_editor_shell &shell, const rv_editor_theme &theme);
void rv_editor_shell_recent_menu(rv_editor_shell &shell);
rv_editor_size rv_editor_recent_minimum();

// The least tile each of the other pages of the start screen draws whole in every state. Call inside a frame or with the
// UI font pushed.
rv_editor_size rv_editor_new_project_minimum();
rv_editor_size rv_editor_open_project_minimum();
rv_editor_size rv_editor_settings_minimum();
rv_editor_size rv_editor_help_minimum();
rv_editor_size rv_editor_manual_minimum();

// The terminal in front, with the keyboard.
void rv_editor_shell_focus_terminal(rv_editor_shell &shell);
void rv_editor_shell_stop_terminal(rv_editor_app &app, rv_editor_terminal_view &view);
// A build that did not start or failed: Problems in front with the keyboard, in the tile of the Output when it is
// not in the layout yet (rv_editor_shell.cpp).
void rv_editor_shell_focus_problems(rv_editor_shell &shell);
// Focus Next / Previous Pane: the next tile in reading order takes the keyboard.
void rv_editor_shell_focus_next(rv_editor_shell &shell, bool back);

// A live shell's tile closes only after this question; drawn after the workspace.
void rv_editor_shell_terminal_answered(rv_editor_shell &shell, rv_editor_pane_id pane);

// A new tile of `kind` beside the focused tile (or the first), half and half.
void rv_editor_shell_new_tile(rv_editor_shell &shell, rv_editor_pane_kind kind);

// A new Untitled buffer in the code tile used last.
void rv_editor_shell_new_file(rv_editor_shell &shell);

// Project > Save All: every modified buffer and the scene.
void rv_editor_shell_save_all(rv_editor_shell &shell);

// Once a frame before drawing. With finish_save disabled, the caller finishes saves after drawing.
void rv_editor_shell_update(rv_editor_shell &shell, bool finish_save = true);

// A pane's title with its context (spec 6.1): what it shows, from which session,
// build or candidate; nothing when the plain kind says it all.
void rv_editor_shell_title(rv_editor_shell &shell, rv_editor_pane_id pane, rv_editor_pane_kind kind);

// Save for a particular code buffer and Save As in the tile chosen by the menu.
void rv_editor_shell_save_buffer(rv_editor_shell &shell, int64_t buffer);
void rv_editor_shell_save_as_start(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_nvim_buffer &buffer);
const char *rv_editor_shell_why_not_save(const rv_editor_shell &shell);

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
// What a code tile asks instead of its grid (nvim keeps its size): the Unsaved Changes ask of the closing tile, Save As
// of its tile. False when nothing is asked and the tile shows its grid.
bool rv_editor_shell_code_asks(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_theme &theme);
void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme);
// The widest row of Review Changes (its shelf at the widest verb, or its message box): what its minimum is as wide as.
float rv_editor_review_content_width();
// Once a frame: Review Changes is a tab while something waits on unsaved files and
// goes when nothing does; Save As whose code tile has gone is dropped.
void rv_editor_shell_review_tab(rv_editor_shell &shell);
// Editor > Editor Settings: the tool paths of settings.toml.
void rv_editor_page_settings(rv_editor_shell &shell, const rv_editor_theme &theme);
// What the override path field of tool `i` drew last, for tests.
const rv_editor_path_probe &rv_editor_page_settings_path_probe(size_t i);
// Once a frame: an Apply asked for saves settings.toml and finds the tools again; a failed save goes to settings_error.
void rv_editor_shell_settings_apply(rv_editor_shell &shell);
// Sets vsync as shell.pace asks on shell.renderer and sets shell.vsync_active from what SDL reports.
// RV_OK, or RV_ERR_IO; the SDL reason is logged as a warning.
int rv_editor_shell_pace_apply(rv_editor_shell &shell);

// Once a frame after drawing: sends the Game's keys while a drawn Game tile
// holds the keyboard of a focused window and a console runs, and every key up
// the moment any of that stops.
void rv_editor_shell_game_input(rv_editor_shell &shell);

// rv_editor_pane_draw_fn for the window's workspace; `context` is the shell.
void rv_editor_shell_pane(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme);

// shell.ws.minimums[pane]: `kind`'s constant minimum (rv_editor_shell_minimums.cpp), never
// what the pane drew or measured; unset when the kind has none. `game_need` is not read.
void rv_editor_shell_set_minimum(rv_editor_shell &shell,
    rv_editor_pane_id pane,
    rv_editor_pane_kind kind,
    rv_editor_size game_need);

// True if UI scale fits on the window's display in 1280*scale x 720*scale pixels.
bool rv_editor_shell_scale_fits(SDL_Window *window, float scale);

// Minimum editor window size at scale 1.0.
inline constexpr int window_min_width = 1280; // pixels required for UI layout
inline constexpr int window_min_height = 720; // pixels required for UI layout

// Display size in pixels.
struct rv_editor_display_size {
    int w_pixels = 0;
    int h_pixels = 0;
};

// Get display size in pixels for the given window's display.
rv_editor_display_size rv_editor_get_display_size(SDL_Window *window);

// Show error message window when display is too small; prints to stderr and exits via SDL_Quit.
void rv_editor_show_too_small_error(SDL_Window *&window, SDL_Renderer *&renderer, const rv_editor_display_size &display_size);

} // namespace rv_editor
