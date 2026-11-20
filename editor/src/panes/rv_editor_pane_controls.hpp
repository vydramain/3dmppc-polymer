// Runtime Controls' pure text rules: the message line under the transport buttons and the facts line.

#pragma once

#include <cstdint>
#include <string>

#include "session/rv_editor_session.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Which answer `detail` carries; none keeps a generic short text.
enum class rv_editor_controls_outcome {
    none,
    reload_accepted,
    reload_refused,
    bake_failed,
};

// What decides the message line. Priority: force_stop, then uncertain, then detail (a reload's or
// a bake's answer), then the Build and Restart note. Nothing set means no message.
struct rv_editor_controls_message_input {
    bool force_stop = false; // Force Stop is offered; hung picks the wording
    bool hung = false;
    bool uncertain = false;                                                // a request went unanswered
    std::string detail;                                                    // a reload or bake answer, may be long
    rv_editor_controls_outcome outcome = rv_editor_controls_outcome::none; // what `detail` is
    bool offer_build_restart = false;                                      // Build and Restart took Reload's slot
    bool disc_cannot_reload = false;                                       // the running disc itself cannot reload
    std::string why_not_reload;                                            // the plan's reason, used when the disc can reload
};

struct rv_editor_controls_message_text {
    std::string brief; // one short line (at most 60 characters), shown in the pane
    std::string full;  // the whole text, shown in the tooltip
};

rv_editor_controls_message_text rv_editor_controls_message(const rv_editor_controls_message_input &in);

// What the status under the buttons says. With a project, parts joined by " | ": the session ("Running, session 1", "Stopped"
// with no session number), then the message's brief when there is one, else the build's step ("Build #7 [v][>][ ] compile")
// when a build is shown (build_number != 0 and a non-empty step).
struct rv_editor_controls_status_input {
    bool project_open = true; // false: the text is "No project is open" and nothing else
    rv_editor_run_state run_state = rv_editor_run_state::stopped;
    uint32_t session_number = 0;
    uint32_t build_number = 0;
    std::string build_step; // rv_editor_steps_compact of the build's run
    rv_editor_controls_message_input message;
};

std::string rv_editor_controls_status(const rv_editor_controls_status_input &in);

// The width the action slot (Force Stop, Cancel Build, the reload badge) after the transport bar always
// reserves: the widest of them, whatever the state, so the bar's tile size never depends on it.
float rv_editor_controls_action_slot_width(const rv_editor_theme &theme);

// The least width of the Runtime Controls strip (six small buttons, then the right column: the action slot with the mark
// note, or the longest message, whichever is wider) and its height at a tile `width` wide. Both measure with the current font.
float rv_editor_transport_column_min_width();
// The width the longest status needs to wrap into its two lines; the status is never narrower.
float rv_editor_transport_status_min_width();
float rv_editor_transport_min_width();
float rv_editor_transport_height(float width);

} // namespace rv_editor
