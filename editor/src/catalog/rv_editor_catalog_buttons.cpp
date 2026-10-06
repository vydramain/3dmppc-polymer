// Catalog section: buttons, icon buttons, toggles, check boxes, radios - one
// row per widget, one column per state; the last column is live.

#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_column
{
    const char *name;
    rv_editor_state state;
};

constexpr rv_editor_column rv_editor_columns[] = {
    { "catalog_buttons.col_normal", { rv_editor_look::normal, nullptr } },
    { "catalog_buttons.col_hovered", { rv_editor_look::hovered, nullptr } },
    { "catalog_buttons.col_pressed", { rv_editor_look::pressed, nullptr } },
    { "catalog_buttons.col_focused", { rv_editor_look::focused, nullptr } },
    { "catalog_buttons.col_disabled", { rv_editor_look::normal, "catalog_buttons.disabled_reason" } },
    { "catalog_buttons.col_live", { rv_editor_look::live, nullptr } },
};

constexpr int rv_editor_column_count = static_cast<int>(sizeof(rv_editor_columns) / sizeof(rv_editor_columns[0]));

// Values the live column edits; the frozen columns show fixed ones.
struct rv_editor_live
{
    bool toggle_off = false;
    bool toggle_on = true;
    bool check_off = false;
    bool check_on = true;
    int radio = 0; // the two radio rows are one group
};

rv_editor_live rv_editor_live_values;

void rv_editor_catalog_row(const char *name, const rv_editor_theme &t, int row)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    for (int c = 0; c < rv_editor_column_count; ++c) {
        ImGui::TableNextColumn();
        ImGui::PushID(row * rv_editor_column_count + c);
        rv_editor_state s = rv_editor_columns[c].state;
        if (s.disabled != nullptr) {
            s.disabled = rv_editor_text(s.disabled);
        }
        const bool live = s.look == rv_editor_look::live && s.disabled == nullptr;
        bool frozen_off = false;
        bool frozen_on = true;
        switch (row) {
        case 0:
            rv_editor_button(rv_editor_text("catalog_buttons.button_ok"), t, s);
            break;
        case 1:
            rv_editor_icon_button("##icon", rv_editor_icon_name::folder, t, s);
            break;
        case 2: {
            const char *snap = rv_editor_text("catalog_buttons.toggle_snap");
            rv_editor_toggle(snap, live ? &rv_editor_live_values.toggle_off : &frozen_off, t, s);
            break;
        }
        case 3: {
            const char *snap = rv_editor_text("catalog_buttons.toggle_snap");
            rv_editor_toggle(snap, live ? &rv_editor_live_values.toggle_on : &frozen_on, t, s);
            break;
        }
        case 4: {
            const char *grid = rv_editor_text("catalog_buttons.checkbox_grid");
            rv_editor_checkbox(grid, live ? &rv_editor_live_values.check_off : &frozen_off, t, s);
            break;
        }
        case 5: {
            const char *grid = rv_editor_text("catalog_buttons.checkbox_grid");
            rv_editor_checkbox(grid, live ? &rv_editor_live_values.check_on : &frozen_on, t, s);
            break;
        }
        case 6: {
            const char *lua_text = rv_editor_text("catalog_buttons.radio_lua");
            if (rv_editor_radio(lua_text, live ? rv_editor_live_values.radio == 0 : false, t, s) && live) {
                rv_editor_live_values.radio = 0;
            }
            break;
        }
        default: {
            const char *cpp_text = rv_editor_text("catalog_buttons.radio_cpp");
            if (rv_editor_radio(cpp_text, live ? rv_editor_live_values.radio == 1 : true, t, s) && live) {
                rv_editor_live_values.radio = 1;
            }
            break;
        }
        }
        ImGui::PopID();
    }
}

} // namespace

void rv_editor_catalog_buttons(const rv_editor_theme &theme)
{
    if (!ImGui::BeginTable("buttons", rv_editor_column_count + 1)) {
        return;
    }
    const char *row_ids[] = { "catalog_buttons.row_button", "catalog_buttons.row_icon_button",
        "catalog_buttons.row_toggle_off", "catalog_buttons.row_toggle_on",
        "catalog_buttons.row_check_off", "catalog_buttons.row_check_on",
        "catalog_buttons.row_radio_off", "catalog_buttons.row_radio_on" };
    const char *longest_label = rv_editor_text("catalog_buttons.row_icon_button");
    ImGui::TableSetupColumn(rv_editor_text("catalog_buttons.column_widget"),
        ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(longest_label).x);
    for (const rv_editor_column &c : rv_editor_columns) {
        ImGui::TableSetupColumn(rv_editor_text(c.name), ImGuiTableColumnFlags_WidthStretch);
    }
    ImGui::TableHeadersRow();

    for (int row = 0; row < static_cast<int>(sizeof(row_ids) / sizeof(row_ids[0])); ++row) {
        rv_editor_catalog_row(rv_editor_text(row_ids[row]), theme, row);
    }
    ImGui::EndTable();
}

} // namespace rv_editor
