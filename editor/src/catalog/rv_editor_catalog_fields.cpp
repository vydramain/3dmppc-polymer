// Catalog section: text fields, spinners and dropdowns - one row per widget,
// one column per field state; the last column is live.

#include <cstring>

#include "catalog/rv_editor_catalog.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_field_column
{
    const char *name;
    rv_editor_field field;
};

const rv_editor_field_column rv_editor_field_columns[] = {
    {"normal", {}},
    {"hovered", {{rv_editor_look::hovered, nullptr}}},
    {"focused", {{rv_editor_look::focused, nullptr}}},
    {"disabled", {{rv_editor_look::normal, "Disabled: shown here to check the look"}}},
    {"invalid", {{rv_editor_look::normal, nullptr}, false, false, "Invalid: shown here to check the look"}},
    {"read-only", {{rv_editor_look::normal, nullptr}, true}},
    {"dirty", {{rv_editor_look::normal, nullptr}, false, true}},
    {"live", {}},
};

constexpr int rv_editor_field_column_count =
    static_cast<int>(sizeof(rv_editor_field_columns) / sizeof(rv_editor_field_columns[0]));

constexpr const char *rv_editor_templates[] = {"Lua", "C++"};

// One value per cell, so typing into one field does not change its neighbours.
struct rv_editor_field_values
{
    char text[rv_editor_field_column_count][32];
    int number[rv_editor_field_column_count];
    int choice[rv_editor_field_column_count];

    rv_editor_field_values()
    {
        for (int c = 0; c < rv_editor_field_column_count; ++c) {
            std::strncpy(text[c], "my-game", sizeof(text[c]));
            number[c] = 320;
            choice[c] = 0;
        }
    }
};

rv_editor_field_values rv_editor_field_data;

} // namespace

void rv_editor_catalog_fields(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Fields");
    if (!ImGui::BeginTable("fields", rv_editor_field_column_count + 1)) {
        return;
    }
    ImGui::TableSetupColumn("widget", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Icon button").x);
    for (const rv_editor_field_column &c : rv_editor_field_columns) {
        ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthStretch);
    }
    ImGui::TableHeadersRow();

    const char *rows[] = {"Text field", "Spinner", "Dropdown"};
    for (int row = 0; row < 3; ++row) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(rows[row]);
        for (int c = 0; c < rv_editor_field_column_count; ++c) {
            ImGui::TableNextColumn();
            ImGui::PushID(row * rv_editor_field_column_count + c);
            ImGui::SetNextItemWidth(-1.0f);
            const rv_editor_field &f = rv_editor_field_columns[c].field;
            switch (row) {
            case 0:
                rv_editor_text_field("##text", rv_editor_field_data.text[c], sizeof(rv_editor_field_data.text[c]),
                    theme, f);
                break;
            case 1:
                rv_editor_spinner("##number", &rv_editor_field_data.number[c], 8, theme, f);
                break;
            default:
                rv_editor_dropdown("##choice", &rv_editor_field_data.choice[c], rv_editor_templates, 2, theme, f);
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace rv_editor
