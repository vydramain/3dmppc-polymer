// The boot screen picture: one olive window with a fixed layout for the whole boot (title, status
// row with its icon, journal panel, button band). Composed into a host buffer; no rv_cv.
#pragma once

#include <cstdint>
#include <vector>

#include "rv_pconsole/rv_pcboot_journal.hpp"
#include "rv_pconsole/rv_pchost_ui.hpp"

namespace rv_3dmppc
{

// A rectangle in frame pixels.
using rv_pcboot_rect = rv_pchost_rect;

// What the status row says and which button the band shows.
enum class rv_pcboot_state {
    starting, // Hourglass, button "Stop for Maintenance"
    warning,  // Started with warnings, continue prompt
    stopped,  // Stopped for maintenance, continue prompt
    error,    // The system cannot start, button "Power off"
};

// The state's button on a frame of width x height; the picture draws it and the click test uses
// the same rectangle. {0, 0, 0, 0} for a state without a button (warning, stopped).
rv_pcboot_rect rv_pcboot_button_rect(int64_t width, int64_t height, rv_pcboot_state state);

// Compose the picture into `out`, resized to width * height and fully written. Text that does not
// fit its area is not drawn, without a log; a small frame is normal, not an error. `unfit`, when not
// null, counts the elements left undrawn for lack of room: the title, the status text and the
// button label or prompt one each; the journal lines from the first one that does not fit on, one
// each; a wrapped text with a word wider than its line once. Meaningful only with RV_OK.
// RV_OK; RV_ERR_INVAL with `out` cleared, for a frame with a side <= 0 or a size that does not fit
// `out`; RV_ERR_NOMEM with `out` cleared, when the allocation fails. The reason (and the size) is
// logged when memory suffices for the log itself.
int rv_pcboot_screen_build(std::vector<uint32_t> &out,
    int64_t width,
    int64_t height,
    const rv_pcboot_journal &journal,
    rv_pcboot_state state,
    int64_t *unfit = nullptr);

} // namespace rv_3dmppc
