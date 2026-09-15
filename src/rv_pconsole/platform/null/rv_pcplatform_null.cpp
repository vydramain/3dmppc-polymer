// null object. A run that wants nothing (or a build with no platform
// library) still gets an rv_pcplatform: every call below is a no-op that
// returns the zeroed/absent answer, so callers never branch on "is there a
// platform".
#include "rv_pconsole/platform/null/rv_pcplatform_null.hpp"

#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

class rv_pcwindow_null final : public rv_pcwindow
{
public:
    int64_t open(const char * /*title*/, int64_t /*screen_width*/, int64_t /*screen_height*/,
        uint64_t /*scale*/) override
    {
        return RV_OK;
    }

    bool presenting() const override
    {
        return false;
    }

    void present(const uint32_t * /*argb*/) override
    {
    }

    bool close_requested() const override
    {
        return false;
    }

    uint64_t keyboard_abilities() const override
    {
        return 0;
    }

    rv_istate keyboard_state() const override
    {
        return rv_istate{};
    }

    rv_imouse consume_mouse() override
    {
        return rv_imouse{};
    }
};

class rv_pcgamepads_null final : public rv_pcgamepads
{
public:
    uint64_t generation() const override
    {
        return 0;
    }

    const std::vector<uint32_t> &connected() const override
    {
        static const std::vector<uint32_t> empty;
        return empty;
    }

    uint64_t abilities(uint32_t /*id*/) const override
    {
        return 0;
    }

    rv_istate state(uint32_t /*id*/) const override
    {
        return rv_istate{};
    }

    int64_t rumble(uint32_t /*id*/, uint16_t /*left*/, uint16_t /*right*/,
        uint16_t /*duration_ms*/) override
    {
        return RV_ERR_INVAL;
    }

    int64_t rumble_triggers(uint32_t /*id*/, uint16_t /*left*/, uint16_t /*right*/,
        uint16_t /*duration_ms*/) override
    {
        return RV_ERR_INVAL;
    }
};

class rv_pcaudio_sink_null final : public rv_pcaudio_sink
{
public:
    bool available() const override
    {
        return false;
    }

    int64_t queued_frames() const override
    {
        return 0;
    }

    void write(const int16_t * /*interleaved*/, int64_t /*frames*/) override
    {
    }
};

class rv_pcplatform_null final : public rv_pcplatform
{
public:
    void pump() override
    {
    }

    rv_pcwindow &window() override
    {
        return window_;
    }

    rv_pcgamepads &gamepads() override
    {
        return gamepads_;
    }

    rv_pcaudio_sink &audio() override
    {
        return audio_;
    }

private:
    rv_pcwindow_null window_;
    rv_pcgamepads_null gamepads_;
    rv_pcaudio_sink_null audio_;
};

} // namespace

std::unique_ptr<rv_pcplatform> rv_pcplatform_null_make(const rv_pcplatform_wants & /*wants*/)
{
    RV_LOG_INFO("pcplatform", "null: no window, no gamepads, no audio device");
    return std::make_unique<rv_pcplatform_null>();
}

} // namespace rv_3dmppc
