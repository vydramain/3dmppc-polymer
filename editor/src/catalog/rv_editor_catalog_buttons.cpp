// Catalog section: buttons, icon buttons, toggles, check boxes, radios - one
// row per widget, one column per state; the last column is live.

#include "catalog/rv_editor_catalog.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

constexpr const char *rv_editor_disabled_reason = "Disabled: shown here to check the look";

struct rv_editor_column
{
    const char *name;
    rv_editor_state state;
};

constexpr rv_editor_column rv_editor_columns[] = {
    {"normal", {rv_editor_look::normal, nullptr}},
    {"hovered", {rv_editor_look::hovered, nullptr}},
    {"pressed", {rv_editor_look::pressed, nullptr}},
    {"focused", {rv_editor_look::focused, nullptr}},
    {"disabled", {rv_editor_look::normal, rv_editor_disabled_reason}},
    {"live", {rv_editor_look::live, nullptr}},
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
        const rv_editor_state &s = rv_editor_columns[c].state;
        const bool live = s.look == rv_editor_look::live && s.disabled == nullptr;
        bool frozen_off = false;
        bool frozen_on = true;
        switch (row) {
        case 0:
            rv_editor_button("OK", t, s);
            break;
        case 1:
            rv_editor_icon_button("##icon", rv_editor_icon_name::folder, t, s);
            break;
        case 2:
            rv_editor_toggle("Snap", live ? &rv_editor_live_values.toggle_off : &frozen_off, t, s);
            break;
        case 3:
            rv_editor_toggle("Snap", live ? &rv_editor_live_values.toggle_on : &frozen_on, t, s);
            break;
        case 4:
            rv_editor_checkbox("Grid", live ? &rv_editor_live_values.check_off : &frozen_off, t, s);
            break;
        case 5:
            rv_editor_checkbox("Grid", live ? &rv_editor_live_values.check_on : &frozen_on, t, s);
            break;
        case 6:
            if (rv_editor_radio("Lua", live ? rv_editor_live_values.radio == 0 : false, t, s) && live) {
                rv_editor_live_values.radio = 0;
            }
            break;
        default:
            if (rv_editor_radio("C++", live ? rv_editor_live_values.radio == 1 : true, t, s) && live) {
                rv_editor_live_values.radio = 1;
            }
            break;
        }
        ImGui::PopID();
    }
}

} // namespace

void rv_editor_catalog_buttons(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Buttons");
    if (!ImGui::BeginTable("buttons", rv_editor_column_count + 1)) {
        return;
    }
    const char *rows[] = {"Button", "Icon button", "Toggle off", "Toggle on", "Check off", "Check on",
        "Radio off", "Radio on"};
    ImGui::TableSetupColumn("widget", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Icon button").x);
    for (const rv_editor_column &c : rv_editor_columns) {
        ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthStretch);
    }
    ImGui::TableHeadersRow();

    for (int row = 0; row < static_cast<int>(sizeof(rows) / sizeof(rows[0])); ++row) {
        rv_editor_catalog_row(rows[row], theme, row);
    }
    ImGui::EndTable();
}

} // namespace rv_editor
