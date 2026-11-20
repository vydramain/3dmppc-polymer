// Files' questions: the short ones in the message box (a name in its field), the delete of a folder as a card that
// takes the tile's content, and the shelf that asks them.

#include "panes/rv_editor_pane_files.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

namespace rv_editor
{

namespace
{

// The most doomed entries a question lists, and the characters of the name field at its narrowest.
constexpr size_t dialog_doomed_max = 12;
constexpr int name_field_chars = 16;

using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;

struct rv_editor_files_request_row {
    rv_editor_files_view::rv_editor_files_dialog dialog;
    rv_editor_pane_request_kind kind;
};

// The request a dialog's confirm leaves for the app.
constexpr rv_editor_files_request_row files_request_rows[] = {
    { rv_editor_files_view::rv_editor_files_dialog::new_file, rv_editor_pane_request_kind::create_file },
    { rv_editor_files_view::rv_editor_files_dialog::new_dir, rv_editor_pane_request_kind::create_dir },
    { rv_editor_files_view::rv_editor_files_dialog::rename, rv_editor_pane_request_kind::rename },
    { rv_editor_files_view::rv_editor_files_dialog::remove, rv_editor_pane_request_kind::remove },
};

// The row of a dialog's request; nullptr for none and for a dialog without a row.
const rv_editor_files_request_row *rv_editor_files_request_find(rv_editor_files_view::rv_editor_files_dialog dialog)
{
    const auto row = std::ranges::find(files_request_rows, dialog, &rv_editor_files_request_row::dialog);
    return row == std::end(files_request_rows) ? nullptr : row;
}

// The question has a request the app answers; a view with a dialog and no row is closed.
const rv_editor_files_request_row *rv_editor_files_question(rv_editor_app &app, rv_editor_files_view &view)
{
    const rv_editor_files_request_row *row =
        view.dialog == dialog_kind::none ? nullptr : rv_editor_files_request_find(view.dialog);
    if (view.dialog != dialog_kind::none && row == nullptr) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "files dialog without a request closed: impossible call");
        view.dialog = dialog_kind::none;
    }
    return row;
}

bool rv_editor_files_target_is_dir(const rv_editor_files_view &view)
{
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::symlink_status(view.target, ec));
}

// "Delete x and everything in it? This cannot be undone. It removes:".
std::string rv_editor_files_confirm_text(const rv_editor_files_view &view)
{
    const std::string what = view.target.filename().string();
    const std::string dir_suffix = rv_editor_files_target_is_dir(view) ? rv_editor_text("pane_files.remove_with_contents") : "";
    return rv_editor_text_format("pane_files.remove_confirm", std::make_format_args(what, dir_suffix));
}

// What a delete lists: the first entries by name and how many more there are.
std::string rv_editor_files_doomed_text(const rv_editor_files_view &view, const char *separator)
{
    std::string out;
    const size_t listed = std::min(view.doomed.size(), dialog_doomed_max);
    for (size_t i = 0; i < listed; ++i) {
        out += (i == 0 ? "" : separator) + view.doomed[i];
    }
    if (view.doomed_total > listed) {
        const size_t more_count = view.doomed_total - listed;
        out += separator + rv_editor_text_format("pane_files.and_more_items", std::make_format_args(more_count));
    }
    return out;
}

// The text of a short question: where it applies, the name asked for, the error.
std::string rv_editor_files_question_text(const rv_editor_files_view &view)
{
    if (view.dialog == dialog_kind::remove) {
        return rv_editor_files_confirm_text(view) + "\n" + rv_editor_files_doomed_text(view, ", ") +
            (view.error.empty() ? "" : "\n" + view.error);
    }
    const char *label = rv_editor_text(view.dialog == dialog_kind::rename ? "pane_files.new_name" : "pane_files.name");
    const std::string where = (view.dialog == dialog_kind::rename ? view.target.parent_path() : view.target).string();
    return rv_editor_text_format("pane_files.dialog_header", std::make_format_args(label, where)) +
        (view.error.empty() ? "" : "\n" + view.error);
}

// The question's confirm sends its request to the app, which runs it on the next frame and writes the outcome into the
// view; Cancel closes it.
void rv_editor_files_answer(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    const rv_editor_files_request_row &row,
    int pressed)
{
    if (pressed == 0 && !rv_editor_app_request_pending(app, row.kind)) {
        (void)rv_editor_app_request(app, { row.kind, pane, view.target, std::string(view.name) });
    } else if (pressed == 1) {
        view.dialog = dialog_kind::none;
    }
}

std::vector<std::string> rv_editor_files_question_buttons(const rv_editor_files_view &view)
{
    return { rv_editor_text(view.dialog == dialog_kind::remove ? "pane_files.delete_button" : "pane_files.ok_button"),
        rv_editor_text("pane_files.cancel_button") };
}

// The widest row the tile has: the shelf of five square buttons, or the message box with its name field.
float rv_editor_files_shelf_items()
{
    return 5.0f * ImGui::GetFrameHeight() + 4.0f * ImGui::GetStyle().ItemSpacing.x;
}

std::vector<std::string> rv_editor_files_card_buttons()
{
    return { rv_editor_text("pane_files.delete_button"), rv_editor_text("pane_files.cancel_button") };
}

} // namespace

// The directory a new entry goes into: `sel` when it is a directory,
// its parent when it is a file, the root otherwise.
std::filesystem::path rv_editor_files_target_dir(rv_editor_app &app, const std::filesystem::path &sel)
{
    if (sel.empty()) {
        return app.files.root().path;
    }
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::symlink_status(sel, ec)) ? sel : sel.parent_path();
}

void rv_editor_files_ask(rv_editor_files_view &view,
    rv_editor_files_view::rv_editor_files_dialog dialog,
    const std::filesystem::path &target,
    const std::string &name)
{
    view.dialog = dialog;
    view.target = target;
    view.error.clear();
    std::memset(view.name, 0, sizeof(view.name));
    std::strncpy(view.name, name.c_str(), sizeof(view.name) - 1);
    view.opening = true;
    view.doomed.clear();
    view.doomed_total = 0;
    if (dialog != rv_editor_files_view::rv_editor_files_dialog::remove) {
        return;
    }
    // Listed once, when asked: the first entries by name and how many there are.
    const std::filesystem::path base = target.parent_path();
    const auto note = [&](const std::filesystem::path &p) {
        if (view.doomed.size() < dialog_doomed_max) {
            std::error_code ec;
            view.doomed.push_back(std::filesystem::relative(p, base, ec).generic_string());
        }
        ++view.doomed_total;
    };
    note(target);
    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(target, ec))) {
        return;
    }
    for (std::filesystem::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        note(it->path());
    }
}

float rv_editor_files_content_width()
{
    return std::max(rv_editor_tiled_shelf_width(rv_editor_files_shelf_items()),
        rv_editor_message_strip_min_width(
            { { rv_editor_text("pane_files.ok_button"), rv_editor_text("pane_files.cancel_button") },
                { rv_editor_text("pane_files.delete_button"), rv_editor_text("pane_files.cancel_button") },
                { rv_editor_text("panes.open_project_box") } },
            name_field_chars));
}

// The delete of a folder: its list is the card's text; the card takes the tile's content.
bool rv_editor_files_card(rv_editor_app &app, rv_editor_pane_id pane, rv_editor_files_view &view, const rv_editor_theme &theme)
{
    const rv_editor_files_request_row *row = rv_editor_files_question(app, view);
    if (row == nullptr || view.dialog != dialog_kind::remove || !rv_editor_files_target_is_dir(view)) {
        return false;
    }
    const rv_editor_mode_card_spec spec{ rv_editor_text("pane_files.dialog_title_remove"),
        rv_editor_files_confirm_text(view) + "\n\n" + rv_editor_files_doomed_text(view, "\n"),
        view.error,
        rv_editor_label_kind::error,
        rv_editor_files_card_buttons() };
    const int pressed = rv_editor_mode_card_begin("##card", spec, theme);
    rv_editor_mode_card_end();
    rv_editor_files_answer(app, pane, view, *row, pressed);
    return true;
}

namespace
{

// What the strip under the tree says: a short question (with the name field for a new name), or the project's absence, or
// nothing (empty text).
struct rv_editor_files_say_text : rv_editor_message_say {
    bool named = false; // the name field in a row of its own
};

rv_editor_files_say_text rv_editor_files_say(rv_editor_app &app, rv_editor_files_view &view)
{
    if (rv_editor_files_question(app, view) == nullptr) {
        if (app.files.is_open()) {
            return {};
        }
        return { { rv_editor_text("panes.no_project_open_message"),
            rv_editor_label_kind::normal,
            { rv_editor_text("panes.open_project_box") } } };
    }
    return { { rv_editor_files_question_text(view),
                 view.error.empty() ? rv_editor_label_kind::normal : rv_editor_label_kind::error,
                 rv_editor_files_question_buttons(view) },
        view.dialog != dialog_kind::remove };
}

} // namespace

float rv_editor_files_strip_height(rv_editor_app &app, rv_editor_files_view &view, float width)
{
    const rv_editor_files_say_text say = rv_editor_files_say(app, view);
    return rv_editor_message_strip_height(say.text, say.buttons, width, say.named);
}

void rv_editor_files_strip(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    float width,
    const rv_editor_theme &theme)
{
    const rv_editor_files_say_text say = rv_editor_files_say(app, view);
    const rv_editor_files_request_row *row = rv_editor_files_question(app, view);
    if (row == nullptr) {
        const int pressed = rv_editor_message_strip("##ask", say.text, say.kind, say.buttons, theme, width);
        app.open_folder_request = app.open_folder_request || pressed == 0;
        return;
    }
    rv_editor_message_strip_field field;
    field.buf = view.name;
    field.size = sizeof(view.name);
    field.hint = rv_editor_text("pane_files.name");
    field.focus = view.opening;
    view.opening = false;
    const char *waiting = rv_editor_app_request_pending(app, row->kind) ? rv_editor_text("app_why_not.files_pending") : nullptr;
    const std::vector<rv_editor_state> states(2, rv_editor_state{ rv_editor_look::live, waiting });
    const int pressed =
        rv_editor_message_strip("##ask", say.text, say.kind, say.buttons, theme, width, &states, say.named ? &field : nullptr);
    rv_editor_files_answer(app, pane, view, *row, pressed);
}

// The shelf: new file and folder, rename, delete and refresh, always there; those that need a selection say so.
void rv_editor_files_shelf(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    const rv_editor_ui_port &ui,
    const rv_editor_theme &theme)
{
    const bool open = app.files.is_open();
    const bool has_sel = open && !ui.state.files_selected.empty() && ui.state.files_selected != app.files.root().path;
    const rv_editor_state no_project{ rv_editor_look::live, open ? nullptr : rv_editor_text("pane_files.no_project_open") };
    const rv_editor_state need_sel = has_sel ?
        rv_editor_state{} :
        rv_editor_state{ rv_editor_look::live,
            open ? rv_editor_text("pane_files.select_first") : rv_editor_text("pane_files.no_project_open") };
    // A refresh waiting for the next frame blocks Refresh, saying so.
    const bool refresh_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::refresh_files);
    const rv_editor_state refresh_state{ rv_editor_look::live,
        !open               ? rv_editor_text("pane_files.no_project_open") :
            refresh_pending ? rv_editor_text("app_why_not.files_pending") :
                              nullptr };
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_letter_button("##new_file",
            rv_editor_glyph::new_,
            theme.code_green,
            rv_editor_text("pane_files.button_new_file"),
            theme,
            no_project)) {
        rv_editor_files_ask(view, dialog_kind::new_file, rv_editor_files_target_dir(app, ui.state.files_selected), "");
    }
    ImGui::SameLine();
    if (rv_editor_letter_button("##new_dir",
            rv_editor_glyph::new_folder,
            theme.code_yellow,
            rv_editor_text("pane_files.button_new_folder"),
            theme,
            no_project)) {
        rv_editor_files_ask(view, dialog_kind::new_dir, rv_editor_files_target_dir(app, ui.state.files_selected), "");
    }
    ImGui::SameLine();
    if (rv_editor_letter_button("##rename",
            rv_editor_glyph::rename,
            theme.code_blue,
            rv_editor_text("pane_files.button_rename"),
            theme,
            need_sel)) {
        rv_editor_files_ask(view, dialog_kind::rename, ui.state.files_selected, ui.state.files_selected.filename().string());
    }
    ImGui::SameLine();
    if (rv_editor_letter_button("##delete",
            rv_editor_glyph::delete_,
            theme.code_red,
            rv_editor_text("pane_files.button_delete"),
            theme,
            need_sel)) {
        rv_editor_files_ask(view, dialog_kind::remove, ui.state.files_selected, "");
    }
    ImGui::SameLine();
    if (rv_editor_letter_button("##refresh",
            rv_editor_glyph::refresh,
            rv_editor_mocha_teal,
            rv_editor_text("pane_files.button_refresh"),
            theme,
            refresh_state)) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::refresh_files, pane });
    }
    rv_editor_shelf_end();
}

ImVec2 rv_editor_files_card_min_size()
{
    return rv_editor_mode_card_min_size(rv_editor_files_card_buttons(), rv_editor_theme_olive);
}

} // namespace rv_editor
