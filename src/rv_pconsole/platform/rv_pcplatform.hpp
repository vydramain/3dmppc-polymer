// The platform axis: what connects the virtual machine to the real machine it
// runs on. It is NOT a PDK slot. cv/ca/cio are the console's virtual hardware
// and never learn which platform serves them; the platform is chosen per run
// ([mode.*] platform = ..., --mode_platform) and offers small host services
// the virtual devices are wired to: a window that shows finished frames and
// receives keyboard and mouse, the physical gamepads, and an audio sink that
// plays the PCM the SPU produced.
//
// A platform never advances virtual state. It shows, plays and reports; the
// console timeline (rv_pconsole::disc_run) is the only thing that moves the
// machine.
//
// null object. A service the run did not ask for (rv_pcplatform_wants)
// or that refused to come up behaves as absent: open() is a successful no-op,
// abilities read 0, the sink is unavailable. Callers never branch on "is there
// a platform".
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/rv_err.h"
#include "rv_pconsole/platform/rv_pcsignals.hpp"

namespace rv_3dmppc
{

// The console's PCM format, fixed for every platform: interleaved stereo
// signed 16-bit at 44100 Hz. rv_pcca_sw produces exactly this.
constexpr int64_t RV_PCPLATFORM_PCM_RATE = 44100;
constexpr int64_t RV_PCPLATFORM_PCM_CHANNELS = 2;

// Which endpoints this run needs, derived by boot from the slots: no window
// when cv is null, no gamepads when cio is null, no audio device when ca is
// null. A platform opens nothing that is not wanted.
struct rv_pcplatform_wants {
    bool window = false;
    bool gamepads = false;
    bool audio = false;
};

// A point in frame pixels: the screen_width x screen_height given to open().
// inside is false when the point is outside the frame or there is no window.
struct rv_pcpointer {
    bool inside = false;
    int64_t x = 0;
    int64_t y = 0;
};

// Operator presses since the previous consume_presses(). escape counts the Esc
// key; keys counts every key press, Esc and Pause included, without auto-repeat;
// mouse_buttons counts presses of any mouse button; last_click is where the last
// primary-button press was, inside is false when no primary press came since the
// previous consume_presses().
struct rv_pcpresses {
    uint32_t escape = 0;
    uint32_t keys = 0;
    uint32_t mouse_buttons = 0;
    rv_pcpointer last_click;
};

// A window a finished frame is shown in, and the keyboard and mouse that only
// exist while such a window does.
class rv_pcwindow
{
public:
    virtual ~rv_pcwindow() = default;

    rv_pcwindow(const rv_pcwindow &) = delete;
    rv_pcwindow &operator=(const rv_pcwindow &) = delete;

    // Create the window for a screen_width x screen_height frame magnified by
    // `scale`. RV_OK when a window now exists or none was wanted; RV_ERR_IO
    // when one was wanted and could not be made. A second call keeps the
    // window: same geometry only sets the title, another geometry is
    // RV_ERR_INVAL and changes nothing.
    virtual int64_t open(const char *title, int64_t screen_width, int64_t screen_height, uint64_t scale) = 0;

    // True while present() reaches a surface.
    virtual bool presenting() const = 0;

    // Show one finished frame: screen_width * screen_height pixels of
    // 0xAARRGGBB, row-major, in the geometry given to open(). No-op when
    // !presenting().
    virtual void present(const uint32_t *argb) = 0;

    // True when another program shows the frames and says itself that the
    // machine is paused (--frame-fd): a pause then presents nothing over them.
    virtual bool embedded() const
    {
        return false;
    }

    // Sticky: the user asked to close the window.
    virtual bool close_requested() const = 0;

    // What the keyboard layout can report, in rv_isource bits. 0 while no
    // window exists: keys only ever reach a window.
    virtual uint64_t keyboard_abilities() const = 0;

    // The keyboard as a port snapshot taken by the last pump(): buttons in
    // rv_isource bits, digital sticks at -1/0/+1. Zeroed while
    // keyboard_abilities() is 0.
    virtual rv_istate keyboard_state() const = 0;

    // Relative mouse motion since the previous call; clears the accumulator.
    // Zero while no window exists.
    virtual rv_imouse consume_mouse() = 0;

    // How many times the operator pressed the physical Pause key since this
    // was last called; clears the counter. A COUNT and not a bool, because
    // two presses landing in the same frame (e.g. two presses between two
    // consume_pause_requests() calls) must not collapse into one toggle - the
    // console has to see both edges to end up paused/unpaused the same way it would have if the frames had not
    // coalesced. An EDGE and not a level like close_requested(): pause is a
    // request to flip a state the console owns, not a fact about the key
    // itself, so a held key must not re-request sixty times a second, and the
    // console must not have to remember the previous frame's key state to
    // tell a request from a hold.
    //
    // This is an operator's act on the machine, the same category as closing
    // the window, not game input - it therefore never reaches rv_cio's port
    // snapshot. Do NOT also map SDL_SCANCODE_PAUSE into the keyboard-to-pad
    // table in rv_pcwindow_sdl3.cpp: a disc would then see a phantom button
    // press every time the operator paused.
    virtual uint32_t consume_pause_requests() = 0;

    // Operator presses for the console's own screens, counted as edges like
    // consume_pause_requests(): two presses in one frame must not collapse.
    // Cleared by the call and never part of cio's port snapshot. A caller that
    // starts a screen calls this once first to drop older presses. The default
    // reports nothing; a platform with a window overrides it, otherwise the
    // console's own screens get no input.
    virtual rv_pcpresses consume_presses()
    {
        return {};
    }

    // Where the cursor was at the last pump(); inside is false while it is
    // outside the frame or no window exists. The default reports nothing; a
    // platform with a window overrides it.
    virtual rv_pcpointer cursor() const
    {
        return {};
    }

protected:
    rv_pcwindow() = default;
};

// The physical gamepads. Which virtual port a pad drives is NOT decided here:
// that is virtual port state and belongs to cio (rv_pccio_platform).
class rv_pcgamepads
{
public:
    virtual ~rv_pcgamepads() = default;

    rv_pcgamepads(const rv_pcgamepads &) = delete;
    rv_pcgamepads &operator=(const rv_pcgamepads &) = delete;

    // Changes whenever a pad connects or disconnects, so a caller can skip
    // reconciling when nothing did.
    virtual uint64_t generation() const = 0;

    // Ids of the connected pads, oldest connection first. An id is never 0.
    virtual const std::vector<uint32_t> &connected() const = 0;

    // Static capability of pad `id` in rv_isource bits; 0 for an unknown id.
    virtual uint64_t abilities(uint32_t id) const = 0;

    // Pad `id` as read by the last pump(), dead zones and trigger thresholds
    // already applied; zeroed for an unknown id.
    virtual rv_istate state(uint32_t id) const = 0;

    // Drive the body (rumble) or trigger (rumble_triggers) motors of pad `id`
    // for `duration_ms`. RV_OK; RV_ERR_INVAL for an unknown id; RV_ERR_IO when
    // the device refused.
    virtual int64_t rumble(uint32_t id, uint16_t left, uint16_t right, uint16_t duration_ms) = 0;
    virtual int64_t rumble_triggers(uint32_t id, uint16_t left, uint16_t right, uint16_t duration_ms) = 0;

    // Presses of any button on any pad since the previous call; cleared by the
    // call. Not part of state(id) or cio's port snapshot. The default reports
    // nothing; a platform with pads overrides it, otherwise the console's own
    // screens get no pad input.
    virtual uint32_t consume_button_presses()
    {
        return 0;
    }

protected:
    rv_pcgamepads() = default;
};

// Where finished PCM goes. The sink plays on the device, pulling what the
// console attached as its source (attach_source); native tops the device up in
// service() on the console loop, web from an SDL callback. Without a device it
// does nothing and never advances the machine.
class rv_pcaudio_sink
{
public:
    // Fills exactly `frames` stereo frames into `interleaved` (2 * frames int16
    // values in the RV_PCPLATFORM_PCM_* format). On web it runs inside SDL's get
    // callback under the stream lock: it must not call back into the sink
    // (pause, resume, attach_source) or any SDL audio API.
    using rv_pcaudio_fill_fn = void (*)(void *context, int16_t *interleaved, int64_t frames);

    virtual ~rv_pcaudio_sink() = default;

    rv_pcaudio_sink(const rv_pcaudio_sink &) = delete;
    rv_pcaudio_sink &operator=(const rv_pcaudio_sink &) = delete;

    // True while a physical device is pulling from the attached source.
    virtual bool available() const = 0;

    // Attach the source the device pulls from; called once by the console.
    // nullptr detaches it. Without a device this stores nothing.
    virtual void attach_source(rv_pcaudio_fill_fn, void *)
    {
    }

    // Called on every pass of the console loop that runs a frame, and before
    // each 1 ms slice of the frame wait; not while paused. Native tops the
    // device queue up from the attached source. Without a device this does
    // nothing.
    virtual void service()
    {
    }

    // Hold the device; its queue is kept. RV_OK; RV_ERR_IO when the device refused.
    virtual int pause()
    {
        return RV_OK;
    }

    // Undo pause(). RV_OK; RV_ERR_IO when the device refused.
    virtual int resume()
    {
        return RV_OK;
    }

protected:
    rv_pcaudio_sink() = default;
};

// Bits of rv_pcplatform::refused(): one per platform service (video, gamepads,
// audio), set when that service was wanted and refused to come up.
enum rv_pcplatform_refusal : uint32_t {
    RV_PCPLATFORM_REFUSED_VIDEO = 0x1,
    RV_PCPLATFORM_REFUSED_GAMEPADS = 0x2,
    RV_PCPLATFORM_REFUSED_AUDIO = 0x4,
};

class rv_pcplatform
{
public:
    virtual ~rv_pcplatform() = default;

    rv_pcplatform(const rv_pcplatform &) = delete;
    rv_pcplatform &operator=(const rv_pcplatform &) = delete;

    // Drain the platform's event queue and re-read every device: window close,
    // pad arrival and removal, mouse motion, then pad and keyboard snapshots.
    // Called at the top of every pass of the console loop (frame_step), paused
    // or not; the loop is driven by rv_pconsole::disc_run (native) or the web
    // main-loop callback. Still the only place platform state changes. Pads keep updating
    // without a window: pumping is a platform duty, not a window one.
    virtual void pump() = 0;

    virtual rv_pcwindow &window() = 0;
    virtual rv_pcgamepads &gamepads() = 0;
    virtual rv_pcaudio_sink &audio() = 0;

    // A normal shutdown was requested: SIGINT/SIGTERM (process-wide, see
    // rv_pcsignals.hpp) or the window was closed.
    bool quit_requested()
    {
        return rv_pcsignals_quit_requested() || window().close_requested();
    }

    // The services the run wanted that refused to come up, as rv_pcplatform_refusal
    // bits. For the boot journal only: nothing branches its behaviour on it. A
    // service not wanted is never refused.
    virtual uint32_t refused() const
    {
        return 0;
    }

protected:
    rv_pcplatform() = default;
};

} // namespace rv_3dmppc
