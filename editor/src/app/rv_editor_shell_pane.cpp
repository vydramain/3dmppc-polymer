// Pane rendering dispatch for all pane kinds.

#include "app/rv_editor_shell.hpp"

#include "imgui.h"

#include "catalog/rv_editor_catalog.hpp"
#include "panes/rv_editor_panes.hpp"

namespace rv_editor
{

namespace
{

// A control strip's width, in font sizes.
constexpr int rv_editor_strip_width_em = 20;

} // namespace

void rv_editor_shell_pane(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme)
{
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(context);
    switch (kind) {
        case rv_editor_pane_kind::catalog: rv_editor_catalog_draw(theme); return;
        case rv_editor_pane_kind::candidate: rv_editor_pane_candidate(shell.app, theme); return;
        case rv_editor_pane_kind::checks: rv_editor_pane_checks(shell.app, theme); return;
        case rv_editor_pane_kind::scene: rv_editor_pane_scene(shell.app, shell.renderer, theme); return;
        case rv_editor_pane_kind::hierarchy: rv_editor_pane_hierarchy(shell.app, theme); return;
        case rv_editor_pane_kind::assets: rv_editor_pane_assets(shell.app, shell.renderer, theme); return;
        case rv_editor_pane_kind::inspector: rv_editor_pane_scene_inspector(shell.app, theme); return;
        case rv_editor_pane_kind::build_result: rv_editor_pane_build_result(shell.app, theme); return;
        case rv_editor_pane_kind::problems: rv_editor_pane_problems(shell.app, theme); return;
        case rv_editor_pane_kind::search: rv_editor_pane_search(shell.app, theme); return;
        case rv_editor_pane_kind::run_config: rv_editor_pane_run_config(shell.app, theme); return;
        case rv_editor_pane_kind::controls:
        case rv_editor_pane_kind::release_controls:
        case rv_editor_pane_kind::toolchest: {
            // Measured each frame: a strip's minimum is what it drew.
            const float top = ImGui::GetCursorPosY();
            if (kind == rv_editor_pane_kind::controls) {
                rv_editor_pane_controls(shell.app, theme);
            } else if (kind == rv_editor_pane_kind::toolchest) {
                rv_editor_pane_toolchest(shell.app, theme);
            } else {
                rv_editor_pane_release_controls(shell.app, theme);
            }
            const float tall = ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
            shell.strips[pane] = { static_cast<int32_t>(ImGui::GetFontSize() * rv_editor_strip_width_em),
                static_cast<int32_t>(tall) };
            return;
        }
        case rv_editor_pane_kind::runtime_log:
        case rv_editor_pane_kind::build_log: {
            // One source each, until the user ticks more.
            const bool runtime = kind == rv_editor_pane_kind::runtime_log;
            rv_editor_output_view view;
            view.show.fill(false);
            view.show[static_cast<size_t>(runtime ? rv_editor_log_source::runtime : rv_editor_log_source::candidate)] =
                true;
            shell.app.outputs.try_emplace(pane, view);
            rv_editor_pane_output(shell.app, pane, theme);
            return;
        }
        case rv_editor_pane_kind::output: rv_editor_pane_output(shell.app, pane, theme); return;
        case rv_editor_pane_kind::project: rv_editor_pane_project(shell.app, theme); return;
        case rv_editor_pane_kind::files: rv_editor_pane_files(shell.app, pane, theme); return;
        case rv_editor_pane_kind::code:
            // A question about this tile's file sits over its text, not over the window.
            if (pane == shell.closing) {
                rv_editor_shell_ask_close(shell, theme);
            } else if (pane == shell.save_as_pane && shell.save_as_buffer != 0 &&
                shell.leaving == rv_editor_shell::rv_editor_leave::none) {
                rv_editor_shell_ask_save_as(shell, theme);
            }
            rv_editor_pane_code(shell.app, pane, theme);
            return;
        case rv_editor_pane_kind::review_changes: rv_editor_page_review(shell, theme); return;
        case rv_editor_pane_kind::terminal:
            if (pane == shell.closing_terminal) {
                rv_editor_shell_ask_terminal(shell, theme);
            }
            rv_editor_pane_terminal(shell.app, pane, theme);
            return;
        case rv_editor_pane_kind::game: rv_editor_pane_game(shell.app, shell.renderer, theme); return;
        case rv_editor_pane_kind::observe: rv_editor_pane_observe(shell.app, theme); return;
        case rv_editor_pane_kind::findings: rv_editor_pane_findings(shell.app, theme); return;
        case rv_editor_pane_kind::session: rv_editor_pane_session(shell.app, theme); return;
        case rv_editor_pane_kind::test_case: rv_editor_pane_test_case(shell.app, theme); return;
        case rv_editor_pane_kind::open_project: rv_editor_page_open_project(shell, theme); return;
        case rv_editor_pane_kind::settings: rv_editor_page_settings(shell, theme); return;
        case rv_editor_pane_kind::help: rv_editor_page_help(shell, theme); return;
        case rv_editor_pane_kind::manual:
            rv_editor_page_manual(shell, theme);
            return;
        case rv_editor_pane_kind::empty:
            rv_editor_pane_empty(shell, pane, theme);
            return;
        default: break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s: this pane kind has no view", rv_editor_pane_title(kind));
    ImGui::PopStyleColor();
}

} // namespace rv_editor
