// Save and history commands for the document selected when the menu opened.

#include <algorithm>

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

const rv_editor_nvim_buffer *rv_editor_menu_buffer(const rv_editor_shell &shell)
{
    if (shell.menu_document.buffer == 0 || !shell.app.nvim.running()) {
        return nullptr;
    }
    const auto &buffers = shell.app.nvim.buffers();
    const auto found = std::ranges::find(buffers, shell.menu_document.buffer, &rv_editor_nvim_buffer::id);
    return found == buffers.end() ? nullptr : &*found;
}

bool rv_editor_menu_scene_current(const rv_editor_shell &shell)
{
    return shell.menu_document.kind == rv_editor_pane_kind::scene && shell.app.scene != nullptr &&
        shell.app.scene->scene.path == shell.menu_document.scene_path;
}

const char *rv_editor_menu_document_missing(const rv_editor_shell &shell)
{
    switch (shell.menu_document.kind) {
    case rv_editor_pane_kind::code:
        return rv_editor_text("shell_document.no_code_buffer");
    case rv_editor_pane_kind::scene:
        return rv_editor_text("shell_menu_scene.no_scene_open");
    default:
        return rv_editor_text("shell_document.no_document");
    }
}

bool rv_editor_document_menu_item(const char *label, const char *shortcut, const char *why_not, const std::string &tip)
{
    const bool clicked = rv_editor_menu_item(label, shortcut, why_not);
    if (why_not == nullptr) {
        rv_editor_item_tooltip(tip.c_str());
    }
    return clicked;
}

void rv_editor_menu_save(rv_editor_shell &shell, const rv_editor_nvim_buffer *buffer, const std::string &name)
{
    const bool scene = rv_editor_menu_scene_current(shell);
    const char *why_not = rv_editor_shell_why_not_save(shell);
    if (why_not == nullptr) {
        if (scene) {
            why_not = rv_editor_scene_why_not_save(shell.app);
        } else if (buffer == nullptr) {
            why_not = rv_editor_menu_document_missing(shell);
        } else if (buffer->name.empty()) {
            why_not = rv_editor_text("shell_document.name_with_save_as");
        } else if (!buffer->modified) {
            why_not = rv_editor_text("shell_menu_scene.nothing_to_save");
        }
    }
    if (rv_editor_document_menu_item(rv_editor_text("shell_menu.save"),
            scene ? rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::save_scene) :
                    rv_editor_text("shell_menu.shortcut_save"),
            why_not,
            rv_editor_text_format("shell_document.save_tip", std::make_format_args(name)))) {
        if (scene) {
            rv_editor_shell_scene_save(shell);
        } else {
            rv_editor_shell_save_buffer(shell, buffer->id);
        }
    }
}

const char *rv_editor_menu_why_not_save_all(const rv_editor_shell &shell)
{
    if (!shell.app.project.open) {
        return rv_editor_text("shell_menu.why_no_project");
    }
    if (const char *why_not = rv_editor_shell_why_not_save(shell)) {
        return why_not;
    }
    if (rv_editor_app_scene_dirty(shell.app)) {
        return nullptr;
    }
    const rv_editor_nvim &nvim = shell.app.nvim;
    if (!std::ranges::any_of(nvim.buffers(), &rv_editor_nvim_buffer::modified)) {
        return rv_editor_text("shell_menu_scene.nothing_to_save");
    }
    return nvim.running() ? nullptr : rv_editor_text("nvim_rpc.not_running");
}

const char *rv_editor_menu_why_not_save_as(const rv_editor_shell &shell, const rv_editor_nvim_buffer *buffer)
{
    if (shell.menu_document.kind != rv_editor_pane_kind::code) {
        return rv_editor_text("shell_document.save_as_code_only");
    }
    if (buffer == nullptr || rv_editor_tile_find(shell.ws.layout, shell.menu_document.pane) == rv_editor_tile_none) {
        return rv_editor_text("shell_document.no_code_buffer");
    }
    return rv_editor_shell_why_not_save(shell);
}

const char *rv_editor_menu_why_not_history(const rv_editor_shell &shell, const rv_editor_nvim_buffer *buffer, bool redo)
{
    const char *empty = rv_editor_text(redo ? "shell_menu_scene.nothing_to_redo" : "shell_menu_scene.nothing_to_undo");
    if (rv_editor_menu_scene_current(shell)) {
        const rv_editor_scene_doc &doc = *shell.app.scene;
        if (!doc.scene.read_only.empty()) {
            return rv_editor_text("shell_menu_scene.scene_is_readonly");
        }
        return (redo ? doc.redo.empty() : doc.undo.empty()) ? empty : nullptr;
    }
    if (buffer == nullptr) {
        return rv_editor_menu_document_missing(shell);
    }
    const auto &history = shell.menu_document.history;
    if (!history || shell.app.nvim.switching_root()) {
        return rv_editor_text("shell_save.waiting_for_nvim");
    }
    return (redo ? history->redo : history->undo) ? nullptr : empty;
}

} // namespace

void rv_editor_menu_document_open(rv_editor_shell &shell, uint32_t menu)
{
    rv_editor_menu_document &document = shell.menu_document;
    if (document.menu == menu) {
        return;
    }
    const uint64_t generation = document.generation + 1;
    document = {};
    document.menu = menu;
    document.generation = generation;
    const rv_editor_workspace &ws = shell.ws;
    if (!shell.app.project.open || ws.focused_leaf >= ws.layout.nodes.size()) {
        return;
    }
    const rv_editor_tile_node &node = ws.layout.nodes[ws.focused_leaf];
    if (node.kind != rv_editor_tile_kind::leaf || node.leaf.active >= node.leaf.tabs.size()) {
        return;
    }
    document.pane = node.leaf.tabs[node.leaf.active];
    document.kind = ws.panes.panes[document.pane].kind;
    if (rv_editor_scene_document_pane(document.kind)) {
        document.kind = rv_editor_pane_kind::scene;
        if (shell.app.scene != nullptr) {
            document.scene_path = shell.app.scene->scene.path;
        }
    } else if (document.kind == rv_editor_pane_kind::code && shell.app.nvim.running()) {
        const auto *buffer = shell.app.nvim.buffer_in(shell.app.nvim.window_for(document.pane));
        if (buffer != nullptr) {
            document.buffer = buffer->id;
        }
    }
}

void rv_editor_menu_document_save(rv_editor_shell &shell)
{
    const rv_editor_nvim_buffer *buffer = rv_editor_menu_buffer(shell);
    const std::string name =
        buffer == nullptr ? rv_editor_app_scene_name(shell.app) : rv_editor_shell_buffer_label(shell.app, buffer->name);
    rv_editor_menu_save(shell, buffer, name);
    if (rv_editor_document_menu_item(rv_editor_text("catalog_status.menu_save_all"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::save_all),
            rv_editor_menu_why_not_save_all(shell),
            rv_editor_text("shell_document.save_all_tip"))) {
        rv_editor_shell_save_all(shell);
    }
    if (rv_editor_document_menu_item(rv_editor_text("shell_menu.save_as"),
            nullptr,
            rv_editor_menu_why_not_save_as(shell, buffer),
            rv_editor_text_format("shell_document.save_as_tip", std::make_format_args(name)))) {
        rv_editor_shell_save_as_start(shell, shell.menu_document.pane, *buffer);
    }
}

void rv_editor_menu_document_history(rv_editor_shell &shell)
{
    const rv_editor_nvim_buffer *buffer = rv_editor_menu_buffer(shell);
    rv_editor_menu_document &document = shell.menu_document;
    if (buffer != nullptr && !document.history_requested && !shell.app.nvim.switching_root()) {
        document.history_requested = true;
        shell.app.nvim.history(buffer->id,
            [&document, generation = document.generation](const rv_editor_nvim_history &history) {
                if (document.generation == generation) {
                    document.history = history;
                }
            });
    }
    const std::string name =
        buffer == nullptr ? rv_editor_app_scene_name(shell.app) : rv_editor_shell_buffer_label(shell.app, buffer->name);
    for (const bool redo : { false, true }) {
        if (rv_editor_document_menu_item(rv_editor_text(redo ? "shell_menu.redo" : "shell_menu.undo"),
                rv_editor_text(redo ? "shell_menu.shortcut_redo" : "shell_menu.shortcut_undo"),
                rv_editor_menu_why_not_history(shell, buffer, redo),
                rv_editor_text_format(redo ? "shell_document.redo_tip" : "shell_document.undo_tip",
                    std::make_format_args(name)))) {
            if (buffer != nullptr) {
                shell.app.nvim.history_step(buffer->id, redo);
            } else if (redo) {
                rv_editor_scene_redo(*shell.app.scene);
            } else {
                rv_editor_scene_undo(*shell.app.scene);
            }
        }
    }
}

} // namespace rv_editor
