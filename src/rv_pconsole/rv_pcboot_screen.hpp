// The boot screen picture: a grey window with the title, an hourglass, the starting
// line, the journal and a raised button. Composed into a host buffer, like the
// pause overlay; no rv_cv.
#pragma once

#include <cstdint>
#include <vector>

#include "rv_pconsole/rv_pcboot_journal.hpp"

namespace rv_3dmppc
{

// A rectangle in frame pixels.
struct rv_pcboot_rect {
    int64_t x;
    int64_t y;
    int64_t w;
    int64_t h;
};

// The "Stop for Maintenance" button on a frame of width x height. The picture draws
// it and the click test uses the same rectangle, so the two cannot disagree.
rv_pcboot_rect rv_pcboot_button_rect(int64_t width, int64_t height);

// Compose the starting picture into `out`, resized to width * height and fully
// written. Journal lines that do not fit the window are not drawn, and the journal
// stops at the first one that does not fit.
// RV_OK; or RV_ERR_INVAL with `out` cleared and the reason logged, for a frame with
// a side <= 0 or a size that does not fit `out`.
int rv_pcboot_screen_build(std::vector<uint32_t> &out, int64_t width, int64_t height, const rv_pcboot_journal &journal);

} // namespace rv_3dmppc
