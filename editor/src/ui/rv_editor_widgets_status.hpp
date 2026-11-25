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

// Status bar: one sunken field per text, left to right.
// What rv_editor_status_bar drew, for tests. The segments' widths depend on the bar's width and the widest texts only,
// never on the shown texts. Shortened text has its full text in `tooltip`.
struct rv_editor_status_probe {
    struct segment {
        float x0 = 0, x1 = 0;
        std::string shown;
        std::string tooltip;
    };
    std::vector<segment> segments;
};

// `widest[i]` is the longest text field i can ever show (its template with fields at their limits); segment i is as wide
// as that text plus an equal share of the free width. If space is tight, the widest segments yield room first.
// Returns the index of the segment clicked this frame, else -1.
int rv_editor_status_bar(const char *const fields[],
    const char *const widest[],
    int count,
    const rv_editor_theme &theme,
    rv_editor_status_probe *probe = nullptr);

// The bar's least width: the widest texts, the segments' paddings, the gaps and the window's side paddings.
float rv_editor_status_bar_min_width(const char *const widest[], int count, const rv_editor_theme &theme);

// Lamp and label; each kind has its own symbol as well as its own colour. `frame_aligned` false draws it one text line high
// instead of a frame high, for a row of text.
void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme, bool frame_aligned = true);

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

// One row of six buttons that never wraps: large squares with labels when the window is wide, else small ones; the
// size follows the window width alone. `reserve` is the width the caller's column to the right of the buttons needs: from
// 640 px the squares shrink to leave it, never past their natural side.
rv_editor_transport_actions
rv_editor_transport_bar(const rv_editor_transport_state &state, const rv_editor_theme &theme, float reserve = 0.0f);

// The side of the `count` transport buttons in a tile `width` wide: the large square from 640 px (scaled) that leaves
// `reserve` to the right of the buttons, else a frame.
float rv_editor_transport_side(float width, float reserve = 0.0f, int count = 6);

// True from 640 px (scaled): the transport's buttons are large and labelled.
bool rv_editor_transport_is_large(float width);

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

} // namespace rv_editor
