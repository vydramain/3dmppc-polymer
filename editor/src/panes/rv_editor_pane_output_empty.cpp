// The empty row of Output, Runtime Log and Build Log: no lines at all, or lines that a filter keeps out of sight, with the
// reset button of that filter in the same row.

#include "panes/rv_editor_panes.hpp"

#include <format>

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor
{

void rv_editor_output_empty_row(rv_editor_output_view &view,
    size_t total,
    rv_editor_output_filter first,
    const rv_editor_theme &theme)
{
    if (total == 0 || first == rv_editor_output_filter::none) {
        (void)rv_editor_content_empty_row(rv_editor_text("pane_output.empty_row"), theme);
        return;
    }
    static constexpr const char *names[] = { "pane_output.filter_source",
        "pane_output.filter_level",
        "pane_output.filter_find",
        "pane_output.filter_clear_view" };
    static constexpr const char *resets[] = { "pane_output.reset_source",
        "pane_output.reset_level",
        "pane_output.reset_find",
        "pane_output.reset_clear_view" };
    const size_t at = static_cast<size_t>(first);
    const char *filter = rv_editor_text(names[at]);
    const std::string text = total == 1 ? rv_editor_text_format("pane_output.hidden_row_one", std::make_format_args(filter))
                                        : rv_editor_text_format("pane_output.hidden_row", std::make_format_args(total, filter));
    if (!rv_editor_content_empty_row(text.c_str(), theme, rv_editor_text(resets[at]))) {
        return;
    }
    switch (first) {
    case rv_editor_output_filter::source:
        view.show = { true, true, true, true, false };
        break;
    case rv_editor_output_filter::level:
        view.level = rv_editor_log_level::info;
        break;
    case rv_editor_output_filter::find:
        view.search[0] = '\0';
        break;
    default:
        view.hide_before = 0;
        break;
    }
}

} // namespace rv_editor
