// Catalog section: menus, context menu and status indicators.

#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

enum class rv_editor_status_view {
    compact,
    comfortable,
    spacious,
};

struct rv_editor_status_values {
    bool snap = true;
    bool grid = false;
    rv_editor_status_view view = rv_editor_status_view::comfortable;
};

rv_editor_status_values rv_editor_status_data;

// The menus are two to a bar from this width in font sizes down, so none runs past the tile's edge.
constexpr float menus_one_bar_em = 30.0f;

void rv_editor_catalog_menu_file()
{
    if (rv_editor_menu_begin(rv_editor_text("catalog_status.menu_file"), nullptr)) {
        rv_editor_menu_item(rv_editor_text("catalog_status.menu_new_project"),
            rv_editor_text("catalog_status.shortcut_new_project"),
            nullptr);
        rv_editor_menu_item(rv_editor_text("catalog_status.menu_open_project"),
            rv_editor_text("catalog_status.shortcut_open_project"),
            nullptr);
        ImGui::Separator();
        rv_editor_menu_item(rv_editor_text("catalog_status.menu_save_all"),
            rv_editor_text("catalog_status.shortcut_save_all"),
            rv_editor_text("catalog_status.save_all_why_not"));
        rv_editor_menu_end();
    }
}

void rv_editor_catalog_menu_scene()
{
    if (rv_editor_menu_begin(rv_editor_text("catalog_status.menu_scene"), nullptr)) {
        rv_editor_menu_check(rv_editor_text("catalog_status.menu_scene_snap"), nullptr, &rv_editor_status_data.snap, nullptr);
        rv_editor_menu_check(rv_editor_text("catalog_status.menu_scene_grid"),
            nullptr,
            &rv_editor_status_data.grid,
            rv_editor_text("catalog_status.scene_grid_why_not"));
        rv_editor_menu_end();
    }
}

void rv_editor_catalog_menu_view()
{
    if (rv_editor_menu_begin(rv_editor_text("catalog_status.menu_view"), nullptr)) {
        if (rv_editor_menu_choice(rv_editor_text("catalog_status.view_compact"),
                nullptr,
                rv_editor_status_data.view == rv_editor_status_view::compact,
                nullptr)) {
            rv_editor_status_data.view = rv_editor_status_view::compact;
        }
        if (rv_editor_menu_choice(rv_editor_text("catalog_status.view_comfortable"),
                nullptr,
                rv_editor_status_data.view == rv_editor_status_view::comfortable,
                nullptr)) {
            rv_editor_status_data.view = rv_editor_status_view::comfortable;
        }
        if (rv_editor_menu_choice(rv_editor_text("catalog_status.view_spacious"),
                nullptr,
                rv_editor_status_data.view == rv_editor_status_view::spacious,
                rv_editor_text("catalog_status.view_spacious_why_not"))) {
            rv_editor_status_data.view = rv_editor_status_view::spacious;
        }
        rv_editor_menu_end();
    }
}

void rv_editor_catalog_menu_run()
{
    if (rv_editor_menu_begin(rv_editor_text("catalog_status.menu_run"), rv_editor_text("catalog_status.run_why_not"))) {
        rv_editor_menu_end();
    }
}

void rv_editor_catalog_menus()
{
    const bool narrow = ImGui::GetContentRegionAvail().x < menus_one_bar_em * ImGui::GetFontSize();
    const int bars = narrow ? 2 : 1;
    for (int bar = 0; bar < bars; ++bar) {
        ImGui::PushID(bar);
        ImGui::BeginChild("##menus",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_MenuBar);
        if (rv_editor_menu_bar_begin()) {
            if (bar == 0) {
                rv_editor_catalog_menu_file();
                rv_editor_catalog_menu_scene();
            }
            if (bar == bars - 1) {
                rv_editor_catalog_menu_view();
                rv_editor_catalog_menu_run();
            }
            rv_editor_menu_bar_end();
        }
        if (bar == bars - 1) {
            ImGui::TextWrapped("%s", rv_editor_text("catalog_status.context_help"));
            if (rv_editor_window_context_menu_begin("##context")) {
                rv_editor_menu_item(rv_editor_text("catalog_status.context_rename"), nullptr, nullptr);
                rv_editor_menu_item(rv_editor_text("catalog_status.context_delete"),
                    rv_editor_text("catalog_status.shortcut_context_delete"),
                    nullptr);
                rv_editor_window_context_menu_end();
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

void rv_editor_catalog_indicators(const rv_editor_theme &t)
{
    const struct {
        const char *text;
        rv_editor_status_kind kind;
    } lamps[] = { { "catalog_status.status_stopped", rv_editor_status_kind::idle },
        { "catalog_status.status_building", rv_editor_status_kind::busy },
        { "catalog_status.status_running", rv_editor_status_kind::active },
        { "catalog_status.status_reload_rejected", rv_editor_status_kind::warning },
        { "catalog_status.status_crashed", rv_editor_status_kind::error } };
    bool first = true;
    for (const auto &lamp : lamps) {
        const char *text = rv_editor_text(lamp.text);
        if (!first) {
            rv_editor_flow(rv_editor_status_width(text, lamp.kind, t));
        }
        rv_editor_status(text, lamp.kind, t);
        first = false;
    }
}

} // namespace

void rv_editor_catalog_menu_samples()
{
    rv_editor_catalog_menus();
}

void rv_editor_catalog_lamps(const rv_editor_theme &theme)
{
    rv_editor_catalog_indicators(theme);
}

} // namespace rv_editor
