#pragma once

// The status, log, transport and ask widgets of the editor: the status bar and its lamps, the
// log area and its rows, the transport bar, and the asks inside a pane. Included by rv_editor_widgets.hpp.
#include <cstdint>
#include <string>
#include <vector>

#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widget_item.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// --- status, log, transport, dialogs --------------------------------------------

enum class rv_editor_status_kind {
    idle,
    busy,
    ok,
    active, // an ongoing activity that is going fine (Running, Reloading)
    warning,
    error,
};

enum class rv_editor_severity {
    info,
    warning,
    error,
};

// Status bar: one sunken field per text, left to right; the first field takes
// the width the others leave.
// What rv_editor_status_bar drew, for tests. The segments' widths depend on the bar's width and the field count only,
// never on the texts; a text that does not fit is ellipsized and `tooltip` holds it whole (else empty).
struct rv_editor_status_probe {
    struct segment {
        float x0 = 0, x1 = 0;
        std::string shown;
        std::string tooltip;
    };
    std::vector<segment> segments;
};

void rv_editor_status_bar(const char *const fields[],
    int count,
    const rv_editor_theme &theme,
    rv_editor_status_probe *probe = nullptr);

// Lamp and label; each kind has its own symbol as well as its own colour.
void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme);

// Width rv_editor_status draws `label` in, mark included; reserve this ahead of a status
// that follows something else on the same row.
float rv_editor_status_width(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme);

// A log's text area: Mocha base under Mocha text, with the Motif scrollbars of
// rv_editor_scroll_begin. Rows go between the two calls; always pair them.
bool rv_editor_log_begin(const char *id, ImVec2 size, const rv_editor_theme &theme);
void rv_editor_log_end(const rv_editor_theme &theme);

// One line of process output inside a log area: time and source dimmed, the
// severity as a coloured tag (INF, WRN, ERR), the text in the log's colour.
void rv_editor_log_row(const char *time,
    const char *source,
    rv_editor_severity severity,
    const char *text,
    const rv_editor_theme &theme);

// Why each transport action is unavailable; nullptr means available.
struct rv_editor_transport_state {
    const char *build;
    const char *run;
    const char *pause;
    const char *step;
    const char *stop;
    const char *reload;
    bool resume = false; // the machine is paused: Run reads Resume
    // Reload's slot shows Build and Restart instead (Reload cannot apply the
    // pending change); `build_restart` is why it is disabled, if it is.
    bool build_restart = false;
    const char *build_restart_disabled = nullptr;
    // Reload's tooltip/menu name when it is shown instead; nullptr: "Reload Entry Script".
    const char *reload_name = nullptr;
};

// Which transport action was clicked this frame.
struct rv_editor_transport_actions {
    bool build;
    bool run;
    bool pause;
    bool step;
    bool stop;
    bool reload;
    bool build_restart;
};

// `reserve`: width of what is drawn right after the bar on the same row (e.g. a status);
// the bar takes the largest tile size that leaves that room, so every button stays shown.
rv_editor_transport_actions
rv_editor_transport_bar(const rv_editor_transport_state &state, const rv_editor_theme &theme, float reserve = 0.0f);

// An area inside a pane that asks something without blocking anything else: a
// bordered block under its own header; the caller draws the text and the buttons
// between the two calls. Always pair them.
void rv_editor_ask_begin(const char *title, const rv_editor_theme &theme);
void rv_editor_ask_end();

// A message line: a constant place `lines` text lines high and `width` wide (0 = what remains of the row), empty when
// `text` is empty, the same rect for any text. The text wraps inside; what needs more than `lines` lines ends with an
// ellipsis and shows in full in a tooltip; a non-null `tip` differing from `text` is shown in that tooltip instead.
// `kind` colours the text: error stays red, warning yellow.
void rv_editor_message_line(const char *id,
    const char *text,
    int lines,
    float width,
    const rv_editor_theme &theme,
    const char *tip = nullptr,
    rv_editor_label_kind kind = rv_editor_label_kind::normal);

// An ask's place: a constant height for the ask's longest variant (`lines` of wrapped text under the header, plus a
// button row). Open it draws the ask inside (title, then what the caller draws between the calls) and returns true.
// Closed (`open` false) it is the same place unseen and untouchable and returns false: a caller that draws its content
// anyway keeps every rect, one that has nothing to keep skips it. Always pair with rv_editor_ask_place_end.
bool rv_editor_ask_place_begin(const char *id, const char *title, int lines, bool open, const rv_editor_theme &theme);
void rv_editor_ask_place_end();

// An ask drawn over the screen rectangle min..max, for the rare case where it covers no tools or information: it takes
// no room in the layout, so items after it keep their rects. Closed it draws nothing and returns false. Always pair.
// It floats above the panes, so the caller picks a rect that hides no tool or information.
bool rv_editor_ask_overlay_begin(const char *id,
    const char *title,
    ImVec2 min,
    ImVec2 max,
    bool open,
    const rv_editor_theme &theme);
void rv_editor_ask_overlay_end();

} // namespace rv_editor
