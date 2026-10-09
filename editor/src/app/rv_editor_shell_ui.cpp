// Panes queue requests that apply after the draw, before game_input. The shell owns the state and writes
// it directly; the menu is drawn before the tiles, so panes see its writes in the same frame.

#include "app/rv_editor_shell.hpp"

#include <cstddef>
#include <iterator>

namespace rv_editor
{

namespace
{

using rv_editor_ui_apply_fn = void (*)(rv_editor_ui &ui, const rv_editor_ui_request &request);

void rv_editor_ui_take_focus(rv_editor_ui &ui, const rv_editor_ui_request &request)
{
    ui.text_focus = request.focus;
}

void rv_editor_ui_capture(rv_editor_ui &ui, const rv_editor_ui_request &)
{
    ui.game_captured = true;
}

void rv_editor_ui_release(rv_editor_ui &ui, const rv_editor_ui_request &)
{
    ui.game_captured = false;
}

void rv_editor_ui_set_scale(rv_editor_ui &ui, const rv_editor_ui_request &request)
{
    ui.game_scale = request.scale;
}

void rv_editor_ui_drawn(rv_editor_ui &ui, const rv_editor_ui_request &)
{
    ui.game_drawn = true;
}

void rv_editor_ui_measured(rv_editor_ui &ui, const rv_editor_ui_request &request)
{
    ui.game_need = request.need;
    ui.game_area = request.area;
}

void rv_editor_ui_select_file(rv_editor_ui &ui, const rv_editor_ui_request &request)
{
    ui.files_selected = request.path;
}

struct rv_editor_ui_row {
    rv_editor_ui_request_kind kind;
    rv_editor_ui_apply_fn apply;
};

// Row i must hold kind i: checked at compile time, so a swapped row does not build.
constexpr rv_editor_ui_row rv_editor_ui_table[] = {
    { rv_editor_ui_request_kind::take_text_focus, rv_editor_ui_take_focus },
    { rv_editor_ui_request_kind::capture_keyboard, rv_editor_ui_capture },
    { rv_editor_ui_request_kind::release_keyboard, rv_editor_ui_release },
    { rv_editor_ui_request_kind::set_game_scale, rv_editor_ui_set_scale },
    { rv_editor_ui_request_kind::game_drawn, rv_editor_ui_drawn },
    { rv_editor_ui_request_kind::game_measured, rv_editor_ui_measured },
    { rv_editor_ui_request_kind::select_file, rv_editor_ui_select_file },
};

constexpr bool rv_editor_ui_table_ok()
{
    if (std::size(rv_editor_ui_table) != static_cast<size_t>(rv_editor_ui_request_kind::count)) {
        return false;
    }
    for (size_t i = 0; i < std::size(rv_editor_ui_table); ++i) {
        if (static_cast<size_t>(rv_editor_ui_table[i].kind) != i) {
            return false;
        }
    }
    return true;
}

static_assert(rv_editor_ui_table_ok(), "rv_editor_ui_table: one row per kind, in enum order");

} // namespace

void rv_editor_shell_ui_apply(rv_editor_shell &shell)
{
    for (const rv_editor_ui_request &request : shell.ui_requests) {
        rv_editor_ui_table[static_cast<size_t>(request.kind)].apply(shell.ui, request);
    }
    shell.ui_requests.clear();
}

} // namespace rv_editor
