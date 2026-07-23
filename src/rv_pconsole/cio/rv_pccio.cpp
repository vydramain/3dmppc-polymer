#include "rv_pccio.hpp"

#include "pdk/rv_err.hpp"

int64_t rv_3dmppc::rv_pccio::iport_count() { return conf_.iport_count; }

// Empty slots legally read as zero (see rv_cio.hpp) — no ports are wired yet,
// so every slot is empty. Only ohaptic has an error channel.
uint64_t rv_3dmppc::rv_pccio::iport_abilities(int64_t) { return 0; }

rv_3dmppc::rv_imouse rv_3dmppc::rv_pccio::imouse() { return {}; }

rv_3dmppc::rv_istate rv_3dmppc::rv_pccio::iport_state(int64_t) { return {}; }

int64_t rv_3dmppc::rv_pccio::ohaptic(int64_t, rv_oheffect) {
    return RV_ERR_IO;
}
