#include "rv_pconsole.hpp"

#include <chrono>
#include <cmath>

#include "pdk/rv_err.hpp"
#include "rv_infra/rv_log.hpp"

rv_3dmppc::rv_ca* rv_3dmppc::rv_pconsole::ca() { return &ca_; }
rv_3dmppc::rv_cd* rv_3dmppc::rv_pconsole::cd() { return &cd_; }
rv_3dmppc::rv_cm* rv_3dmppc::rv_pconsole::cm() { return &cm_; }
rv_3dmppc::rv_cio* rv_3dmppc::rv_pconsole::cio() { return &cio_; }
rv_3dmppc::rv_cv* rv_3dmppc::rv_pconsole::cv() { return &cv_; }

// rv_3dmppc::rv_de& rv_3dmppc::rv_pconsole::disc_load(const char* /*path*/) {};

int64_t rv_3dmppc::rv_pconsole::disc_run(rv_3dmppc::rv_de& disc) {
    int64_t dir = disc.disc_initialize(*this);
    if (0 > dir) {
        RV_LOG_ERR("pconsole",
                   "Can't initialize mppcdisc in console. Please check mppcdisc consistency: {}",
                   dir);
        return rv_3dmppc::RV_ERR_INVAL;
    }

    std::chrono::duration<float> dt{};
    std::chrono::time_point t_prev = std::chrono::steady_clock::now();
    do {
        std::chrono::time_point t_now = std::chrono::steady_clock::now();
        dt = t_now - t_prev;
        t_prev = t_now;

        disc.frame_update(dt.count());
        disc.frame_render();
    } while (!disc.disc_release());

    return rv_3dmppc::RV_OK;
}
