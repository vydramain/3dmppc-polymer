// What the panes share: dimmed text, a clock reading, a row of facts and the no-project row.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Millisecond extraction modulus for time formatting.
constexpr size_t ms_modulo = 1000;
// Time formatting buffer capacity.
constexpr size_t time_buffer_size = 32;

} // namespace

void rv_editor_dim(const std::string &text, const rv_editor_theme &theme)
{
    rv_editor_label(text.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
}

std::string rv_editor_clock(std::chrono::system_clock::time_point at)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(at);
    std::tm tm{};
    localtime_r(&t, &tm);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count() % ms_modulo;
    char buf[time_buffer_size];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

// One label and its value on a row of a two-column table.
void rv_editor_fact(const char *label, const std::string &value, const rv_editor_theme &theme)
{
    rv_editor_table_row();
    rv_editor_table_cell();
    rv_editor_label(label, rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    rv_editor_table_cell();
    rv_editor_label(value.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
}

void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_label(rv_editor_text("panes.no_project_open_message"), rv_editor_label_kind::normal, theme);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("panes.open_project_button")));
    if (rv_editor_button(rv_editor_text("panes.open_project_button"), theme)) {
        app.open_folder_request = true;
    }
}

} // namespace rv_editor
