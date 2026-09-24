// Catalog section: menus, context menu, dialog, status indicators, log rows and
// the transport bar.

#include "catalog/rv_editor_catalog.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_status_values
{
    bool snap = true;
};

rv_editor_status_values rv_editor_status_data;

void rv_editor_catalog_menus()
{
    rv_editor_menu_style_push();
    ImGui::BeginChild("##menus", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("New Project", "Ctrl+N");
            ImGui::MenuItem("Open Directory", "Ctrl+O");
            ImGui::Separator();
            ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            ImGui::MenuItem("Snap", nullptr, &rv_editor_status_data.snap);
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled();
        ImGui::BeginMenu("Run");
        ImGui::EndDisabled();
        ImGui::EndMenuBar();
    }
    ImGui::TextUnformatted("Right-click here for a context menu");
    if (ImGui::BeginPopupContextWindow("##context")) {
        ImGui::MenuItem("Rename", "F2");
        ImGui::MenuItem("Delete", "Del");
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    rv_editor_menu_style_pop();
}

// The dialog's look inline, so it can be checked without opening it; the button
// opens the real modal.
void rv_editor_catalog_dialog(const rv_editor_theme &t)
{
    const float width = ImGui::CalcTextSize("Save changes to main.lua before closing?").x;
    ImGui::BeginChild("##dialog", ImVec2(width + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    rv_editor_pane_header("Close project", true, t);
    ImGui::TextUnformatted("Save changes to main.lua before closing?");
    rv_editor_button("Save", t);
    ImGui::SameLine();
    rv_editor_button("Discard", t);
    ImGui::SameLine();
    rv_editor_button("Cancel", t);
    ImGui::EndChild();

    if (rv_editor_button("Open the dialog", t)) {
        ImGui::OpenPopup("Close project");
    }
    if (rv_editor_dialog_begin("Close project", t)) {
        ImGui::TextUnformatted("Save changes to main.lua before closing?");
        if (rv_editor_button("Cancel", t)) {
            ImGui::CloseCurrentPopup();
        }
        rv_editor_dialog_end();
    }
}

void rv_editor_catalog_indicators(const rv_editor_theme &t)
{
    rv_editor_status("Stopped", rv_editor_status_kind::idle, t);
    ImGui::SameLine();
    rv_editor_status("Building", rv_editor_status_kind::busy, t);
    ImGui::SameLine();
    rv_editor_status("Running", rv_editor_status_kind::ok, t);
    ImGui::SameLine();
    rv_editor_status("Reload rejected", rv_editor_status_kind::warning, t);
    ImGui::SameLine();
    rv_editor_status("Crashed", rv_editor_status_kind::error, t);
}

void rv_editor_catalog_log(const rv_editor_theme &t)
{
    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    rv_editor_log_row("12:00:01", "burn", rv_editor_severity::info, "[4/4] burn build/example-lua.discdir", t);
    rv_editor_log_row("12:00:02", "mppc", rv_editor_severity::info, "entry_revision=1 frame=120 mode=paused", t);
    rv_editor_log_row("12:00:03", "disc", rv_editor_severity::warning, "texture 'tone' is not resident", t);
    rv_editor_log_row("12:00:04", "mppc", rv_editor_severity::error, "script_error: main.lua:12: ')' expected", t);
    ImGui::EndChild();
}

void rv_editor_catalog_transport(const rv_editor_theme &t)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Stopped:");
    ImGui::SameLine();
    rv_editor_transport_bar({nullptr, nullptr, "Nothing is running", "Nothing is running", "Nothing is running",
                                "Nothing is running"},
        t);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Paused: ");
    ImGui::SameLine();
    // Run is also Resume, so a paused session can run again.
    rv_editor_transport_bar({nullptr, nullptr, "Already paused", nullptr, nullptr, nullptr}, t);
}

} // namespace

void rv_editor_catalog_status(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Menus");
    rv_editor_catalog_menus();
    ImGui::SeparatorText("Dialog");
    rv_editor_catalog_dialog(theme);
    ImGui::SeparatorText("Status");
    rv_editor_catalog_indicators(theme);
    rv_editor_catalog_log(theme);
    ImGui::SeparatorText("Transport");
    rv_editor_catalog_transport(theme);
}

} // namespace rv_editor
