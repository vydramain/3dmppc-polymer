#pragma once

namespace rv_3dmppc {

// Mouse look sample: motion since the previous poll, in pixels (relative, not an
// absolute cursor position). The console defines the axis orientation.
struct rv_imouse {
    int diff_x, diff_y;
};

}  // namespace rv_3dmppc
