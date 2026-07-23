#include "rv_pconsole.hpp"

rv_3dmppc::rv_ca* rv_3dmppc::rv_pconsole::ca() { return &ca_; }
rv_3dmppc::rv_cd* rv_3dmppc::rv_pconsole::cd() { return &cd_; }
rv_3dmppc::rv_cm* rv_3dmppc::rv_pconsole::cm() { return &cm_; }
rv_3dmppc::rv_cio* rv_3dmppc::rv_pconsole::cio() { return &cio_; }
rv_3dmppc::rv_cv* rv_3dmppc::rv_pconsole::cv() { return &cv_; }

// rv_3dmppc::rv_de& rv_3dmppc::rv_pconsole::disc_load(const char* /*path*/) {};

int64_t rv_3dmppc::rv_pconsole::disc_run(rv_3dmppc::rv_de& disc) {
    disc.disc_initialize(*this);
    return rv_3dmppc::RV_OK;
}
