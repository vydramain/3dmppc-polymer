// Catalog section: text fields, spinners and dropdowns - one row per widget,
// one column per field state; the last column is live.

#include <cstring>

#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Text field buffer size
constexpr size_t text_field_buffer_size = 32;

// Spinner increment step
constexpr int spinner_step = 8;

// Demo value for the catalog's number spinners
constexpr int sample_spinner_value = 320;

// Widget row indices in the fields catalog
constexpr int row_text_field = 0;
constexpr int row_spinner = 1;
constexpr int row_dropdown = 2;

struct rv_editor_field_column
{
    const char *name;
    rv_editor_field field;
};

const rv_editor_field_column rv_editor_field_columns[] = {
    { "catalog_fields.col_normal", {} },
    { "catalog_fields.col_hovered", { { rv_editor_look::hovered, nullptr } } },
    { "catalog_fields.col_focused", { { rv_editor_look::focused, nullptr } } },
    { "catalog_fields.col_disabled", { { rv_editor_look::normal, "catalog_fields.disabled_reason" } } },
    { "catalog_fields.col_invalid", { { rv_editor_look::normal, nullptr }, false, false, "catalog_fields.invalid_reason" } },
    { "catalog_fields.col_read_only", { { rv_editor_look::normal, nullptr }, true } },
    { "catalog_fields.col_dirty", { { rv_editor_look::normal, nullptr }, false, true } },
    { "catalog_fields.col_live", {} },
};

constexpr int rv_editor_field_column_count =
    static_cast<int>(sizeof(rv_editor_field_columns) / sizeof(rv_editor_field_columns[0]));

// One value per cell, so typing into one field does not change its neighbours.
struct rv_editor_field_values
{
    char text[rv_editor_field_column_count][text_field_buffer_size];
    int number[rv_editor_field_column_count];
    int choice[rv_editor_field_column_count];

    rv_editor_field_values()
    {
        for (int c = 0; c < rv_editor_field_column_count; ++c) {
            std::strncpy(text[c], "my-game", sizeof(text[c]));
            number[c] = sample_spinner_value;
            choice[c] = 0;
        }
    }
};

rv_editor_field_values rv_editor_field_data;

} // namespace

void rv_editor_catalog_fields(const rv_editor_theme &theme)
{
    if (!ImGui::BeginTable("fields", rv_editor_field_column_count + 1)) {
        return;
    }
    const char *longest_label = rv_editor_text("catalog_buttons.row_icon_button");
    ImGui::TableSetupColumn(rv_editor_text("catalog_fields.column_widget"),
        ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(longest_label).x);
    for (const rv_editor_field_column &c : rv_editor_field_columns) {
        ImGui::TableSetupColumn(rv_editor_text(c.name), ImGuiTableColumnFlags_WidthStretch);
    }
    ImGui::TableHeadersRow();

    const char *row_ids[] = { "catalog_fields.row_text_field", "catalog_fields.row_spinner",
        "catalog_fields.row_dropdown" };
    for (int row = 0; row < static_cast<int>(sizeof(row_ids) / sizeof(row_ids[0])); ++row) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(rv_editor_text(row_ids[row]));
        for (int c = 0; c < rv_editor_field_column_count; ++c) {
            ImGui::TableNextColumn();
            ImGui::PushID(row * rv_editor_field_column_count + c);
            ImGui::SetNextItemWidth(-1.0f);
            rv_editor_field f = rv_editor_field_columns[c].field;
            if (f.state.disabled != nullptr) {
                f.state.disabled = rv_editor_text(f.state.disabled);
            }
            if (f.invalid != nullptr) {
                f.invalid = rv_editor_text(f.invalid);
            }
            switch (row) {
            case row_text_field:
                rv_editor_text_field("##text", rv_editor_field_data.text[c], sizeof(rv_editor_field_data.text[c]),
                    theme, f);
                break;
            case row_spinner:
                rv_editor_spinner("##number", &rv_editor_field_data.number[c], spinner_step, theme, f);
                break;
            case row_dropdown: {
                const char *templates[] = { rv_editor_text("catalog_fields.template_lua"),
                    rv_editor_text("catalog_fields.template_cpp") };
                rv_editor_dropdown("##choice", &rv_editor_field_data.choice[c], templates,
                    static_cast<int>(std::size(templates)), theme, f);
                break;
            }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace rv_editor
