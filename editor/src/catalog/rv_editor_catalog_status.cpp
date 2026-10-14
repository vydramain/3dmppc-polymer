// Catalog section: menus, context menu, dialog, status indicators, log rows and
// the transport bar.

#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Number of text lines to display in the log area
constexpr float log_display_rows = 4.0f;

// Multiplier for vertical padding in log display (applied to WindowPadding.y)
constexpr float log_padding_y_multiplier = 2.0f;

struct rv_editor_status_values {
    bool snap = true;
};

rv_editor_status_values rv_editor_status_data;

void rv_editor_catalog_menus()
{
    rv_editor_menu_style_push();
    ImGui::BeginChild("##menus",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(rv_editor_text("catalog_status.menu_file"))) {
            ImGui::MenuItem(rv_editor_text("catalog_status.menu_new_project"),
                rv_editor_text("catalog_status.shortcut_new_project"));
            ImGui::MenuItem(rv_editor_text("catalog_status.menu_open_project"),
                rv_editor_text("catalog_status.shortcut_open_project"));
            ImGui::Separator();
            ImGui::MenuItem(rv_editor_text("catalog_status.menu_save_all"),
                rv_editor_text("catalog_status.shortcut_save_all"),
                false,
                false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(rv_editor_text("catalog_status.menu_scene"))) {
            ImGui::MenuItem(rv_editor_text("catalog_status.menu_scene_snap"), nullptr, &rv_editor_status_data.snap);
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled();
        ImGui::BeginMenu(rv_editor_text("catalog_status.menu_run"));
        ImGui::EndDisabled();
        ImGui::EndMenuBar();
    }
    ImGui::TextUnformatted(rv_editor_text("catalog_status.context_help"));
    if (ImGui::BeginPopupContextWindow("##context")) {
        ImGui::MenuItem(rv_editor_text("catalog_status.context_rename"),
            rv_editor_text("catalog_status.shortcut_context_rename"));
        ImGui::MenuItem(rv_editor_text("catalog_status.context_delete"),
            rv_editor_text("catalog_status.shortcut_context_delete"));
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    rv_editor_menu_style_pop();
}

// The area that asks inside a pane, as a question about unsaved files shows it:
// the editor has no modal dialogs.
void rv_editor_catalog_dialog(const rv_editor_theme &t)
{
    rv_editor_ask_begin(rv_editor_text("catalog_status.dialog_unsaved_title"), t);
    ImGui::TextUnformatted(rv_editor_text("catalog_status.dialog_unsaved_message"));
    rv_editor_button(rv_editor_text("catalog_status.dialog_button_save"), t);
    ImGui::SameLine();
    rv_editor_button(rv_editor_text("catalog_status.dialog_button_discard"), t);
    ImGui::SameLine();
    rv_editor_button(rv_editor_text("catalog_status.dialog_button_keep_open"), t);
    rv_editor_ask_end();
}

void rv_editor_catalog_indicators(const rv_editor_theme &t)
{
    rv_editor_status(rv_editor_text("catalog_status.status_stopped"), rv_editor_status_kind::idle, t);
    ImGui::SameLine();
    rv_editor_status(rv_editor_text("catalog_status.status_building"), rv_editor_status_kind::busy, t);
    ImGui::SameLine();
    rv_editor_status(rv_editor_text("catalog_status.status_running"), rv_editor_status_kind::active, t);
    ImGui::SameLine();
    rv_editor_status(rv_editor_text("catalog_status.status_reload_rejected"), rv_editor_status_kind::warning, t);
    ImGui::SameLine();
    rv_editor_status(rv_editor_text("catalog_status.status_crashed"), rv_editor_status_kind::error, t);
}

void rv_editor_catalog_log(const rv_editor_theme &t)
{
    const float rows =
        log_display_rows * ImGui::GetTextLineHeightWithSpacing() + log_padding_y_multiplier * ImGui::GetStyle().WindowPadding.y;
    rv_editor_log_begin("##log", ImVec2(0.0f, rows), t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_1"),
        rv_editor_text("catalog_status.log_source_burn"),
        rv_editor_severity::info,
        rv_editor_text("catalog_status.log_msg_build"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_2"),
        rv_editor_text("catalog_status.log_source_mppc"),
        rv_editor_severity::info,
        rv_editor_text("catalog_status.log_msg_runtime"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_3"),
        rv_editor_text("catalog_status.log_source_disc"),
        rv_editor_severity::warning,
        rv_editor_text("catalog_status.log_msg_missing_texture"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_4"),
        rv_editor_text("catalog_status.log_source_mppc"),
        rv_editor_severity::error,
        rv_editor_text("catalog_status.log_msg_error"),
        t);
    rv_editor_log_end(t);
}

void rv_editor_catalog_transport(const rv_editor_theme &t)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("catalog_status.transport_stopped"));
    ImGui::SameLine();
    const char *transport_nothing = rv_editor_text("catalog_status.transport_nothing_running");
    rv_editor_transport_bar({ nullptr, nullptr, transport_nothing, transport_nothing, transport_nothing, transport_nothing },
        t);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("catalog_status.transport_paused"));
    ImGui::SameLine();
    // Run is also Resume, so a paused session can run again.
    rv_editor_transport_bar(
        { nullptr, nullptr, rv_editor_text("catalog_status.transport_already_paused"), nullptr, nullptr, nullptr },
        t);
    const char *const status_fields[] = { rv_editor_text("catalog_status.status_bar_ready"),
        rv_editor_text("catalog_status.status_bar_runtime"),
        rv_editor_text("catalog_status.status_bar_position") };
    rv_editor_status_bar(status_fields, static_cast<int>(std::size(status_fields)), t);
}

} // namespace

void rv_editor_catalog_menus_dialogs(const rv_editor_theme &theme)
{
    rv_editor_catalog_menus();
    rv_editor_catalog_dialog(theme);
}

void rv_editor_catalog_lamps(const rv_editor_theme &theme)
{
    rv_editor_catalog_indicators(theme);
}

void rv_editor_catalog_logs(const rv_editor_theme &theme)
{
    rv_editor_catalog_log(theme);
}

void rv_editor_catalog_transports(const rv_editor_theme &theme)
{
    rv_editor_catalog_transport(theme);
}

} // namespace rv_editor
