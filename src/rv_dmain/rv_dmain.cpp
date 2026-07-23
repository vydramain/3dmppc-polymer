#include "rv_dmain.hpp"

namespace rv_3dmppc {

int64_t rv_dmain::disc_initialize(rv_pdko& /*pdk*/) {
    // TODO(you): stash &pdk, query geometry via cv()/ca()/cm()/cio(),
    // return a negative rv_err on a mismatch with this disc's expectations.
    return RV_OK;
}

void rv_dmain::frame_update(float) {}

void rv_dmain::frame_render() {}

const char* rv_dmain::disc_title() const { return "skeleton"; }

}  // namespace rv_3dmppc
