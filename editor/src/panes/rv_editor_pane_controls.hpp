// Runtime Controls' pure text rules: the message line under the transport buttons and the facts line.

#pragma once

#include <string>

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

// The width the action slot (Force Stop, Cancel Build, the reload badge) after the transport bar always
// reserves: the widest of them, whatever the state, so the bar's tile size never depends on it.
float rv_editor_controls_action_slot_width(const rv_editor_theme &theme);

// The least width of the Runtime Controls strip (six small buttons, then the right column: the action slot with the mark
// note, or the longest message, whichever is wider) and its height at a tile `width` wide. Both measure with the current font.
float rv_editor_transport_column_min_width();
float rv_editor_transport_min_width();
float rv_editor_transport_height(float width);

// The facts line of a live session; empty when not live or in the Debug preset.
// Never names a target build, the run state or the last run.
struct rv_editor_facts_input {
    bool live = false;
    bool debug_preset = false;
    int frame = 0;
    int session = 0;
    int build = 0;
    bool has_lua = false; // the script revision is shown only then
    int revision = 0;
    int first_revision = 0;
};

std::string rv_editor_facts_line(const rv_editor_facts_input &in);

} // namespace rv_editor
