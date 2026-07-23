#pragma once

#include <cstdint>

#include "pdk/de/rv_de.hpp"

namespace rv_3dmppc {

// Skeleton test disc: the smallest thing the console can boot.
class rv_dmain : public rv_de {
   public:
    rv_dmain() = default;
    ~rv_dmain() = default;

    int64_t disc_initialize(rv_pdko& pdk) override;
    void frame_update(float dt) override;
    void frame_render() override;
    const char* disc_title() const override;
};

}  // namespace rv_3dmppc
