// What a stopped console shows.
//
// Its own file because it is the only PRESENTATION the console does on its own
// behalf. Everything else it puts on screen came from the disc; this is the one
// picture the machine draws about itself, and it has nothing to do with the
// frame loop that decides when to draw it or with the command channel that can
// ask for the stop.
//
// It also has no business being reachable from a disc: pdk/ knows nothing about
// it, and it never touches rv_cv. The overlay is composed into a plain host
// pixel buffer and handed to the window, so no virtual VRAM is allocated and
// the disc's own framebuffer is not written.
#pragma once

#include <cstdint>
#include <vector>

namespace rv_3dmppc
{

// Compose "the console is stopped" into `out`: the frame in `frame` dimmed by
// half, with the label across the middle. `frame` may be null - a run stopped
// before it drew anything gets the label on black.
//
// `out` is resized to width * height and fully written, so the caller may hand
// the same vector back on every pause without clearing it.
//
// A COPY of the frame and never the frame itself: the disc's last picture has to
// stay exactly what the disc drew, or --dump-frame would start reporting what
// the operator was looking at, and a second pause would print over the first.
void rv_pcpause_overlay_build(std::vector<uint32_t> &out, const uint32_t *frame, int64_t width,
    int64_t height);

} // namespace rv_3dmppc
