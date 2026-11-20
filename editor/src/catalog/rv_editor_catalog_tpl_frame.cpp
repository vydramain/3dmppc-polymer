// Template sections of the Widget Catalog, part one: the tab row, the shelf, the list, the records and the message strip.

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "catalog/rv_editor_catalog_templates.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

namespace
{

const auto T = rv_editor_catalog_tpl_text;

// --- tabs ---------------------------------------------------------------------

const char *const tabs_states[] = { "st_tabs_1", "st_tabs_2", "st_tabs_3", "st_tabs_4" };
int tabs_active[4] = { 0, 0, 9, 0 };

std::vector<std::string> tab_names(int state)
{
    switch (state) {
    case 0:
        return { T("tab_one") };
    case 1:
        return { T("tab_few_1"), T("tab_few_2"), T("tab_few_3"), T("tab_few_4") };
    case 2: {
        std::vector<std::string> names;
        for (int i = 0; i < 12; ++i) {
            names.push_back(std::vformat(T("tab_many_format"), std::make_format_args(i)));
        }
        return names;
    }
    default:
        return { T("tab_long_1"), T("tab_long_2"), T("tab_long_3") };
    }
}

void tabs_draw(int state, const rv_editor_theme &theme)
{
    const std::vector<std::string> names = tab_names(state);
    std::vector<const char *> labels;
    for (const std::string &name : names) {
        labels.push_back(name.c_str());
    }
    rv_editor_tab_strip("##tabs", labels.data(), static_cast<int>(labels.size()), &tabs_active[state], theme);
    rv_editor_content_begin("##body", ImVec2(0.0f, 0.0f), theme);
    ImGui::TextUnformatted(T("tab_body"));
    rv_editor_content_end(theme);
}

ImVec2 tabs_min(const rv_editor_theme &)
{
    return rv_editor_catalog_tpl_window(ImVec2(14.0f * ImGui::GetFontSize(),
        rv_editor_tab_strip_height() + ImGui::GetStyle().ItemSpacing.y + rv_editor_content_min_height()));
}

// --- shelf --------------------------------------------------------------------

const char *const shelf_states[] = { "st_shelf_1", "st_shelf_2", "st_shelf_3" };

void shelf_draw(int state, const rv_editor_theme &theme)
{
    const bool some_disabled = state == 1;
    const bool busy = state == 2;
    const char *select = some_disabled ? T("shelf_why_select") : nullptr;
    rv_editor_shelf_begin("##shelf", theme);
    rv_editor_button(T("shelf_new_file"), theme);
    ImGui::SameLine();
    rv_editor_button(T("shelf_new_folder"), theme);
    ImGui::SameLine();
    rv_editor_button(T("shelf_rename"), theme, { .disabled = select });
    ImGui::SameLine();
    rv_editor_button(T("shelf_delete"), theme, { .disabled = select });
    ImGui::SameLine();
    rv_editor_button_fixed(busy ? T("shelf_refreshing") : T("shelf_refresh"),
        { T("shelf_refresh"), T("shelf_refreshing") },
        theme,
        { .disabled = busy ? T("shelf_why_busy") : nullptr });
    ImGui::SameLine();
    rv_editor_button(T("shelf_copy_path"), theme);
    rv_editor_shelf_end();
    rv_editor_content_begin("##tree", ImVec2(0.0f, 0.0f), theme);
    for (int i = 0; i < 8; ++i) {
        const std::string row = std::vformat(T("shelf_row_format"), std::make_format_args(i));
        rv_editor_list_row(("##row" + std::to_string(i)).c_str(), row.c_str(), theme, { .selected = i == 2 });
    }
    rv_editor_content_end(theme);
}

ImVec2 shelf_min(const rv_editor_theme &theme)
{
    const char *labels[] = { T("shelf_new_file"),
        T("shelf_new_folder"),
        T("shelf_rename"),
        T("shelf_delete"),
        T("shelf_copy_path") };
    float row =
        rv_editor_button_width_for({ T("shelf_refresh"), T("shelf_refreshing") }) + 5.0f * ImGui::GetStyle().ItemSpacing.x;
    for (const char *label : labels) {
        row += rv_editor_button_width(label);
    }
    // The shelf's padding is the theme's pad on each side; the content is the shelf row, the gap and the shortest well.
    return rv_editor_catalog_tpl_window(ImVec2(row + 2.0f * theme_pad(theme) + 1.0f,
        ImGui::GetFrameHeight() + 2.0f * theme_pad(theme) + ImGui::GetStyle().ItemSpacing.y + rv_editor_content_min_height()));
}

// --- list ---------------------------------------------------------------------

const char *const list_states[] = { "st_list_1", "st_list_2", "st_list_3", "st_list_4" };

void list_draw(int state, const rv_editor_theme &theme)
{
    const int rows = state == 0 ? 0 : state == 1 ? 3 : state == 2 ? 40 : 4;
    rv_editor_text_item(state == 0 ? " " : T("list_summary"), rv_editor_label_colour(rv_editor_label_kind::error, theme));
    rv_editor_content_begin("##well", ImVec2(0.0f, 0.0f), theme);
    const rv_editor_table_column columns[] = {
        { .name = T("list_col_level"), .width = rv_editor_table_width::fixed, .pinned_left = true },
        { .name = T("list_col_line"), .width = rv_editor_table_width::fixed, .pinned_left = true },
        { .name = T("list_col_message"), .width = rv_editor_table_width::fixed }
    };
    rv_editor_table_style style;
    style.header = true;
    style.sizing = rv_editor_table_sizing::fixed_fit;
    if (rv_editor_table_begin("##table", columns, 3, style)) {
        if (rows == 0) {
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_content_empty_row(T("list_empty"), theme);
        }
        for (int i = 0; i < rows; ++i) {
            const std::string path =
                state == 3 ? T("list_path_long") : std::vformat(T("list_path_format"), std::make_format_args(i));
            const std::string text = path + "  " + (state == 3 ? T("list_message_long") : T("list_message"));
            rv_editor_table_row();
            rv_editor_table_cell();
            ImGui::TextUnformatted(i % 3 == 0 ? T("list_level_error") : T("list_level_warning"));
            rv_editor_table_cell();
            ImGui::Text("%d:4", 12 + i);
            rv_editor_table_cell();
            ImGui::TextUnformatted(text.c_str());
        }
        rv_editor_table_end();
    }
    rv_editor_content_end(theme);
}

ImVec2 list_min(const rv_editor_theme &)
{
    // The summary line is whole at the minimum.
    return rv_editor_catalog_tpl_window(ImVec2(std::max(16.0f * ImGui::GetFontSize(), ImGui::CalcTextSize(T("list_summary")).x),
        ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y + rv_editor_content_min_height()));
}

// --- records ------------------------------------------------------------------

const char *const records_states[] = { "st_records_1", "st_records_2", "st_records_3", "st_records_4" };

void records_draw(int state, const rv_editor_theme &theme)
{
    rv_editor_content_begin("##well", ImVec2(0.0f, 0.0f), theme);
    if (state == 3) {
        rv_editor_content_empty_row(T("rec_empty"), theme);
    } else {
        const char *words[] = { T("rec_available"), T("rec_missing") };
        rv_editor_record_style style;
        style.status_width = rv_editor_record_status_width(words, 2);
        const char *names[] = { T("rec_name_1"), T("rec_name_2"), state == 1 ? T("rec_name_long") : T("rec_name_3") };
        const char *paths[] = { T("rec_path_1"), T("rec_path_2"), T("rec_path_1") };
        for (int i = 0; i < 3; ++i) {
            const bool missing = state == 2 && i == 2;
            style.selected = i == 0;
            rv_editor_record_row(("##record" + std::to_string(i)).c_str(),
                names[i],
                paths[i],
                missing ? words[1] : words[0],
                missing ? rv_editor_status_kind::error : rv_editor_status_kind::ok,
                theme,
                style);
        }
    }
    rv_editor_content_end(theme);
}

ImVec2 records_min(const rv_editor_theme &)
{
    return rv_editor_catalog_tpl_window(ImVec2(20.0f * ImGui::GetFontSize(), rv_editor_content_min_height()));
}

// --- message strip ------------------------------------------------------------

const char *const message_states[] = { "st_message_1",
    "st_message_2",
    "st_message_3",
    "st_message_4",
    "st_message_5",
    "st_message_6" };

std::vector<std::vector<std::string>> message_button_rows()
{
    return { { T("msg_show"), T("msg_dismiss") }, { T("msg_delete"), T("msg_keep") } };
}

void message_draw(int state, const rv_editor_theme &theme)
{
    const char *texts[] = { "", T("msg_info"), T("msg_error"), T("msg_long_error"), T("msg_ask"), T("msg_long_ask") };
    const rv_editor_label_kind kinds[] = { rv_editor_label_kind::normal,
        rv_editor_label_kind::info,
        rv_editor_label_kind::error,
        rv_editor_label_kind::error,
        rv_editor_label_kind::normal,
        rv_editor_label_kind::normal };
    std::vector<std::string> buttons;
    if (state >= 4) {
        buttons = message_button_rows()[1];
    } else if (state >= 2) {
        buttons = message_button_rows()[0];
    }
    const float strip = rv_editor_message_strip_height(texts[state], buttons, ImGui::GetContentRegionAvail().x);
    (void)rv_editor_tiled_area_begin("##well", 0.0f, theme, {}, false, 0.0f, strip);
    rv_editor_list_row("##row", T("shelf_row_format"), theme);
    rv_editor_tiled_area_end(theme);
    rv_editor_message_strip("##strip", texts[state], kinds[state], buttons, theme);
}

ImVec2 message_min(const rv_editor_theme &)
{
    // The strip at the minimum width shows its longest text whole, with the buttons of its row.
    const float w = rv_editor_message_box_min_width(message_button_rows());
    const auto &rows = message_button_rows();
    const float strip = std::max(rv_editor_message_strip_height(T("msg_long_error"), rows[0], w),
        rv_editor_message_strip_height(T("msg_long_ask"), rows[1], w));
    return rv_editor_catalog_tpl_window(ImVec2(w, strip + ImGui::GetStyle().ItemSpacing.y + rv_editor_content_min_height()));
}

} // namespace

RV_EDITOR_TPL(tabs);
RV_EDITOR_TPL(shelf);
RV_EDITOR_TPL(list);
RV_EDITOR_TPL(records);
RV_EDITOR_TPL(message);

} // namespace rv_editor
