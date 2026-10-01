// Scene in the main menu: the scene document's commands, and the New Scene area
// (its state lives here, file-local; the Scene pane draws it).

#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_new_scene_state
{
    bool open = false;
    char name[64] = {};
    bool write_cpp = false;
    std::string error;
    bool focus_name = false;
};

rv_editor_new_scene_state g_new_scene;

// Letters, digits, '_' or '-', non-empty: what rv_editor_app_scene_create checks too.
bool rv_editor_new_scene_name_valid(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

// The header's identifier, mirroring rv_editor_scene_codegen_write's own rule
// (scene/rv_editor_scene_codegen.cpp): non [A-Za-z0-9_] becomes '_', a leading
// digit gets a '_' prefix, so the shown path is exactly the one it would write.
std::string rv_editor_new_scene_header_id(std::string_view name)
{
    std::string id;
    id.reserve(name.size() + 1);
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name.front()))) {
        id.push_back('_');
    }
    for (const char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        id.push_back((std::isalnum(uc) || c == '_') ? c : '_');
    }
    return id;
}

} // namespace

void rv_editor_shell_new_scene_request(const rv_editor_app &app)
{
    g_new_scene.open = true;
    g_new_scene.write_cpp = false;
    g_new_scene.error.clear();
    g_new_scene.focus_name = true;
    std::snprintf(g_new_scene.name, sizeof(g_new_scene.name), "%s", rv_editor_app_scene_free_name(app).c_str());
}

void rv_editor_scene_new_area(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!g_new_scene.open) {
        return;
    }
    rv_editor_ask_begin("New Scene", theme);
    // Escape closes it while this pane (or the name field in it) has the keyboard.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_new_scene.open = false;
        rv_editor_ask_end();
        return;
    }
    ImGui::Text("Name");
    if (g_new_scene.focus_name) {
        ImGui::SetKeyboardFocusHere();
        g_new_scene.focus_name = false;
    }
    rv_editor_text_field("##new_scene_name", g_new_scene.name, sizeof(g_new_scene.name), theme);

    const std::string name = g_new_scene.name;
    std::error_code ec;
    const char *disabled = !rv_editor_new_scene_name_valid(name) ? "Name must be letters, digits, _ or -"
        : std::filesystem::exists(app.project.root / "scenes" / (name + ".scene.toml"), ec)
        ? "A scene with this name already exists"
        : nullptr;

    const std::string header = "src/" + rv_editor_new_scene_header_id(name) + "_scene.hpp";
    const rv_editor_state cpp_state{ rv_editor_look::live, app.project.has_build_section ? nullptr : "Not a C++ disc" };
    rv_editor_checkbox(("Write C++ that loads it (" + header + ")").c_str(), &g_new_scene.write_cpp, theme, cpp_state);

    if (!g_new_scene.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", g_new_scene.error.c_str());
        ImGui::PopStyleColor();
    }

    const rv_editor_state create_state{ rv_editor_look::live, disabled };
    const bool create = rv_editor_button("Create", theme, create_state);
    ImGui::SameLine();
    const bool cancel = rv_editor_button("Cancel", theme);
    if (create) {
        std::string error;
        if (rv_editor_app_scene_create(app, name, g_new_scene.write_cpp, error)) {
            g_new_scene.open = false;
        } else {
            g_new_scene.error = error;
        }
    } else if (cancel) {
        g_new_scene.open = false;
    }
    rv_editor_ask_end();
}

// The scene document's commands (LAY-07); each edit is one undo step.
void rv_editor_menu_scene(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    const char *no_project = app.project.open ? nullptr : "No project is open";
    if (rv_editor_menu_item("New Scene", nullptr, no_project)) {
        rv_editor_shell_new_scene_request(app);
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::scene);
    }
    if (ImGui::BeginMenu("Open Scene", app.project.open)) {
        const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
        for (const std::filesystem::path &path : files) {
            if (ImGui::MenuItem(path.filename().string().c_str())) {
                rv_editor_app_scene_open(app, path);
            }
        }
        if (files.empty()) {
            ImGui::TextDisabled("No scenes/*.scene.toml in this project");
        }
        ImGui::EndMenu();
    }
    const char *no_scene = app.scene == nullptr ? "No scene is open" : nullptr;
    const char *why_not_save = no_scene != nullptr ? no_scene
        : !app.scene->scene.read_only.empty()      ? "The scene is read-only"
        : !app.scene->dirty                        ? "Nothing to save"
                                                   : nullptr;
    if (rv_editor_menu_item("Save Scene", nullptr, why_not_save)) {
        std::string error;
        if (!rv_editor_app_scene_save(app, error)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not saved: " + error);
        }
    }
    ImGui::Separator();
    if (rv_editor_menu_item("Undo Scene Edit", nullptr,
            no_scene != nullptr ? no_scene : app.scene->undo.empty() ? "Nothing to undo" : nullptr)) {
        rv_editor_scene_undo(*app.scene);
    }
    if (rv_editor_menu_item("Redo Scene Edit", nullptr,
            no_scene != nullptr ? no_scene : app.scene->redo.empty() ? "Nothing to redo" : nullptr)) {
        rv_editor_scene_redo(*app.scene);
    }
    ImGui::Separator();
    // New objects go under the selected group, else at the root.
    const char *read_only = no_scene != nullptr ? no_scene
        : !app.scene->scene.read_only.empty()   ? "The scene is read-only"
                                                : nullptr;
    if (ImGui::BeginMenu("Add", read_only == nullptr)) {
        const int sel = rv_editor_scene_find(app.scene->scene, app.scene->selected);
        const std::string parent = sel >= 0 && app.scene->scene.objects[static_cast<size_t>(sel)].kind == "group"
            ? app.scene->selected
            : std::string();
        for (const char *kind : { "group", "camera", "mesh", "quad", "billboard", "volume" }) {
            const std::string label = std::string(kind) == "mesh" ? "Box"
                : std::string(kind) == "camera"                  ? "Camera"
                : std::string(kind) == "quad"                    ? "Quad"
                : std::string(kind) == "billboard"                ? "Billboard"
                : std::string(kind) == "volume"                   ? "Volume"
                                                                   : "Group";
            if (ImGui::MenuItem(label.c_str())) {
                rv_editor_scene_add(*app.scene, kind, parent);
            }
        }
        ImGui::EndMenu();
    }
    const char *no_selection = read_only != nullptr ? read_only
        : app.scene->selected.empty()             ? "Nothing is selected"
                                                  : nullptr;
    if (rv_editor_menu_item("Duplicate", nullptr, no_selection)) {
        rv_editor_scene_duplicate(*app.scene, app.scene->selected);
    }
    if (rv_editor_menu_item("Delete", nullptr, no_selection)) {
        rv_editor_scene_delete(*app.scene, app.scene->selected);
    }
    if (rv_editor_menu_item("Move to Root", nullptr, no_selection)) {
        std::string why;
        if (!rv_editor_scene_reparent(*app.scene, app.scene->selected, "", true, why)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "not moved: " + why);
        }
    }
}

} // namespace rv_editor
