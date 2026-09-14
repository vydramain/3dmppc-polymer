#include "rv_pconsole/cio/rv_pccio_std.hpp"

#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

namespace
{

// A window that either has no keyboard (default) or reports fixed abilities
// and state set by the test.
class rv_pccio_std_selfcheck_window final : public rv_pcwindow
{
public:
    int64_t open(const char *, int64_t, int64_t, uint64_t) override { return RV_OK; }
    bool presenting() const override { return false; }
    void present(const uint32_t *) override { }
    bool close_requested() const override { return false; }

    uint64_t keyboard_abilities() const override { return abilities; }
    rv_istate keyboard_state() const override { return state; }
    rv_imouse consume_mouse() override { return rv_imouse{}; }

    uint64_t abilities = 0;
    rv_istate state{};
};

// Physical pads, connected/disconnected by the test. Abilities and state are
// derived from the id so each fake pad is distinguishable.
class rv_pccio_std_selfcheck_gamepads final : public rv_pcgamepads
{
public:
    uint64_t generation() const override { return generation_; }
    const std::vector<uint32_t> &connected() const override { return connected_; }

    uint64_t abilities(uint32_t id) const override
    {
        for (uint32_t c : connected_) {
            if (c == id) {
                return static_cast<uint64_t>(id) * 10;
            }
        }
        return 0;
    }

    rv_istate state(uint32_t id) const override
    {
        rv_istate out{};
        for (uint32_t c : connected_) {
            if (c == id) {
                out.buttons = id;
            }
        }
        return out;
    }

    int64_t rumble(uint32_t id, uint16_t, uint16_t, uint16_t) override
    {
        for (uint32_t c : connected_) {
            if (c == id) {
                last_rumbled = id;
                return RV_OK;
            }
        }
        return RV_ERR_INVAL;
    }

    int64_t rumble_triggers(uint32_t id, uint16_t, uint16_t, uint16_t) override
    {
        return rumble(id, 0, 0, 0);
    }

    void connect(uint32_t id)
    {
        connected_.push_back(id);
        ++generation_;
    }

    void disconnect(uint32_t id)
    {
        for (auto it = connected_.begin(); it != connected_.end(); ++it) {
            if (*it == id) {
                connected_.erase(it);
                break;
            }
        }
        ++generation_;
    }

    std::vector<uint32_t> connected_;
    uint64_t generation_ = 0;
    uint32_t last_rumbled = 0;
};

} // namespace

bool rv_pccio_std_selfcheck()
{
    rv_pccio_conf conf;
    conf.iport_count = 2;

    rv_pccio_std_selfcheck_window window;
    rv_pccio_std_selfcheck_gamepads gamepads;
    rv_pccio_std cio(conf, window, gamepads);

    // (a) no window, no pads: no keyboard advertised without a window.
    if (cio.iport_abilities(0) != 0) {
        RV_LOG_ERR("pccio", "selfcheck: (a) port 0 abilities not 0 with no window/pads");
        return false;
    }
    rv_istate zero_state = cio.iport_state(0);
    if (zero_state.buttons != 0 || zero_state.left_stick.x != 0.0F
        || zero_state.left_stick.y != 0.0F) {
        RV_LOG_ERR("pccio", "selfcheck: (a) port 0 state not zero with no window/pads");
        return false;
    }

    // (b) window with keyboard abilities/state.
    window.abilities = 0x7;
    window.state.buttons = 0x5;
    window.state.left_stick = rv_iaxes{ 1.0F, 0.0F };

    if (cio.iport_abilities(0) != window.abilities) {
        RV_LOG_ERR("pccio", "selfcheck: (b) port 0 abilities do not match keyboard");
        return false;
    }
    if (cio.iport_abilities(1) != 0) {
        RV_LOG_ERR("pccio", "selfcheck: (b) port 1 abilities not 0 (no keyboard overlay there)");
        return false;
    }
    rv_istate kb_state = cio.iport_state(0);
    if ((kb_state.buttons & window.state.buttons) != window.state.buttons) {
        RV_LOG_ERR("pccio", "selfcheck: (b) port 0 state missing keyboard buttons");
        return false;
    }
    if (kb_state.left_stick.x != 1.0F || kb_state.left_stick.y != 0.0F) {
        RV_LOG_ERR("pccio", "selfcheck: (b) port 0 left_stick not overlaid by keyboard");
        return false;
    }

    // (c) pads: connect 7 then 9.
    gamepads.connect(7);
    gamepads.connect(9);

    if (cio.iport_abilities(0) != (window.abilities | gamepads.abilities(7))) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 0 did not adopt gamepad 7");
        return false;
    }
    if (cio.iport_abilities(1) != gamepads.abilities(9)) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 1 did not adopt gamepad 9");
        return false;
    }

    // Disconnect 7: port 0 empties, port 1 keeps 9.
    gamepads.disconnect(7);
    if (cio.iport_abilities(0) != window.abilities) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 0 not emptied after gamepad 7 disconnected");
        return false;
    }
    if (cio.iport_abilities(1) != gamepads.abilities(9)) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 1 changed after unrelated disconnect");
        return false;
    }

    // Connect 11: fills the empty port 0.
    gamepads.connect(11);
    if (cio.iport_abilities(0) != (window.abilities | gamepads.abilities(11))) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 0 did not adopt gamepad 11");
        return false;
    }

    // Connect 12 with both ports full: ignored.
    gamepads.connect(12);
    if (cio.iport_abilities(0) != (window.abilities | gamepads.abilities(11))) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 0 changed when every slot was full");
        return false;
    }
    if (cio.iport_abilities(1) != gamepads.abilities(9)) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 1 changed when every slot was full");
        return false;
    }

    // Disconnect 11 to free port 0 again: pad 12, which was ignored while
    // every slot was full, is adopted into the freed slot.
    gamepads.disconnect(11);
    if (cio.iport_abilities(0) != (window.abilities | gamepads.abilities(12))) {
        RV_LOG_ERR("pccio", "selfcheck: (c) port 0 did not adopt previously-ignored gamepad 12");
        return false;
    }

    // (d) ohaptic.
    rv_oheffect effect{};
    effect.type = RV_HAPTIC_EFFECT_BASIC_RUMBLE;

    // Port 1 has pad 9: RV_OK and the fake saw pad 9.
    gamepads.last_rumbled = 0;
    if (cio.ohaptic(1, effect) != RV_OK || gamepads.last_rumbled != 9) {
        RV_LOG_ERR("pccio", "selfcheck: (d) ohaptic on occupied port did not reach the pad");
        return false;
    }

    // Empty port: disconnect 12 too, so port 0 is genuinely empty, then
    // ohaptic there.
    gamepads.disconnect(12);
    if (cio.ohaptic(0, effect) != RV_ERR_INVAL) {
        RV_LOG_ERR("pccio", "selfcheck: (d) ohaptic on empty port did not return RV_ERR_INVAL");
        return false;
    }

    // Out-of-range port.
    if (cio.iport_abilities(2) != 0) {
        RV_LOG_ERR("pccio", "selfcheck: (d) out-of-range port abilities not 0");
        return false;
    }
    rv_istate oob_state = cio.iport_state(2);
    if (oob_state.buttons != 0) {
        RV_LOG_ERR("pccio", "selfcheck: (d) out-of-range port state not zero");
        return false;
    }
    if (cio.ohaptic(2, effect) != RV_ERR_INVAL) {
        RV_LOG_ERR("pccio", "selfcheck: (d) ohaptic on out-of-range port did not return RV_ERR_INVAL");
        return false;
    }

    RV_LOG_INFO("pccio", "selfcheck ok");
    return true;
}

} // namespace rv_3dmppc
