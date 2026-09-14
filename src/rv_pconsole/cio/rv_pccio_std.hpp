#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cio/rv_ohaptic.h"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pcwindow;
class rv_pcgamepads;

class rv_pccio_std final : public rv_pccio
{
public:
    // `window` and `gamepads` are borrowed: the console owns the platform and
    // outlives every controller it hands to a disc.
    rv_pccio_std(const rv_pccio_conf &conf, rv_pcwindow &window, rv_pcgamepads &gamepads)
        : conf_(conf)
        , window_(window)
        , gamepads_(gamepads)
        , ports_(conf.iport_count > 0 ? static_cast<size_t>(conf.iport_count) : 0)
    {
    }

    // RV_OK plus the peak host bytes this class allocates for `budget`:
    // iport_count * sizeof(rv_pccio_std_port) — ports_, one rv_pccio_std_port
    // per port.
    static int64_t evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes);

    int64_t iport_count() override;

    uint64_t iport_abilities(int64_t port) override;

    rv_imouse imouse() override;

    rv_istate iport_state(int64_t port) override;

    int64_t ohaptic(int64_t port, rv_oheffect effect) override;

    bool valid() const override { return true; }

private:
    // True when `port` names one of the console's fixed slots. Out of range is
    // NOT an error for the query methods (rv_cio.hpp): they report zeroes.
    bool port_in_range(int64_t port) const
    {
        return port >= 0 && port < conf_.iport_count;
    }

    // A stable virtual port slot. 0 = empty; otherwise the id of the physical
    // pad currently occupying it (rv_pcgamepads ids are never 0).
    struct rv_pccio_std_port {
        uint32_t pad = 0;
    };

    // Apply pad arrivals and departures since the last call. A pad keeps its
    // port until it disconnects; a new pad takes the first empty port or is
    // ignored until it reconnects.
    void reconcile();

    // What the keyboard layout can report, in rv_isource bits — 0 while no
    // window exists, logged whenever it changes.
    uint64_t keyboard_abilities();

    rv_pccio_conf conf_;
    rv_pcwindow &window_;
    rv_pcgamepads &gamepads_;

    std::vector<rv_pccio_std_port> ports_;

    // Pads connected at the last reconcile; any other id is an arrival.
    std::vector<uint32_t> seen_pads_;

    // Forces the first query to reconcile: rv_pcgamepads::generation() is
    // never guaranteed to start away from this value otherwise.
    uint64_t seen_generation_ = ~UINT64_C(0);

    // Last logged keyboard-overlay state; -1 = nothing logged yet.
    int keyboard_seen_ = -1;
};

} // namespace rv_3dmppc
