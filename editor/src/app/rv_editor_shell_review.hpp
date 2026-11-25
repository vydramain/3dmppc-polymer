#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rv_editor
{

struct rv_editor_app;
struct rv_editor_shell;
struct rv_editor_theme;

// Closing, opening and quitting require a fresh check. Build and Run only need the saved files.
enum class rv_editor_review_action {
    none,
    quit,
    open,
    close_project,
    build,
    run,
    build_restart,
};

enum class rv_editor_review_step {
    idle,
    check,
    checking,
    authorized,
    discard,
    opening,
};

struct rv_editor_review {
    uint64_t generation = 0;
    uint64_t input_generation = 0;
    int return_active_frame = -1;
    rv_editor_review_step step = rv_editor_review_step::idle;
    std::filesystem::path target;
    std::string error;
};

bool rv_editor_review_leaves_project(rv_editor_review_action action);
void rv_editor_shell_begin_review(rv_editor_shell &shell, rv_editor_review_action action);
void rv_editor_shell_cancel_review(rv_editor_shell &shell);
void rv_editor_shell_request_leave(rv_editor_shell &shell, rv_editor_review_action action, std::filesystem::path target = {});
void rv_editor_shell_review_save(rv_editor_shell &shell);
void rv_editor_shell_open_project(rv_editor_shell &shell, const std::filesystem::path &path, bool discard);
void rv_editor_shell_close_project(rv_editor_shell &shell);

// Checks for unsaved code and scene changes before opening a picked project.
void rv_editor_shell_request_open(rv_editor_shell &shell, const std::filesystem::path &path);
// Applies a finished save or an authorized transition. Run after drawing so Return goes first.
void rv_editor_shell_after_save(rv_editor_shell &shell);
// A buffer's file as the project sees it: relative to the root when inside it,
// "Untitled" without a name.
std::string rv_editor_shell_buffer_label(const rv_editor_app &app, const std::string &name);
// Saves `ids` (every modified buffer when empty). The outcome lands in
// shell.save_failed, or shell.save_done when every file was written.
void rv_editor_shell_save(rv_editor_shell &shell, const std::vector<int64_t> &ids);
// The scene first, as it saves at once; RV_OK on success or when already saved, RV_ERR_* on failure.
int rv_editor_shell_save_scene(rv_editor_shell &shell);
// Save As under a question or on its own: a card over the tile with the path field in its text area, Save and Cancel.
void rv_editor_shell_save_as_card(rv_editor_shell &shell, const rv_editor_theme &theme);
// What did not save, and why, as lines of the area it is drawn in; a Save As for each Untitled file among them.
void rv_editor_shell_failures(rv_editor_shell &shell, const rv_editor_theme &theme);

} // namespace rv_editor
