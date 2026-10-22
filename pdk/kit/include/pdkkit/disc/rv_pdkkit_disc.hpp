#pragma once

// The C++ disc base of pdkkit: rv_cppdisc's disc, the built-in ECS components, the frame clock and port 0's input.
// A disc derives from pdkkit_disc and calls the base hooks by name (name hiding, as RV_MPPC_DISC_CPP_DEF does).
// A disc that hides disc_initialize or frame_update must call pdkkit_disc's version first, or it silently loses
// pdk_, the built-ins, the clock, the input and the MENU release edge.
// Lifetime: the world keeps non-owning pointers to entity types, and a member of the disc dies before the world, so an
// entity type a disc registers must have static storage (a namespace-scope object), not be a member of the disc.

#include <cstdint>

#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdkkit/component/rv_component_register.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"

namespace rv_pdkkit
{

// Log tag of the disc's refusals.
inline constexpr const char *pdkkit_disc_tag = "pdkkit-disc";

// The player's port, read for the input. rv_cppdisc requires at least one port, so port 0 always exists.
inline constexpr int64_t pdkkit_input_port = 0;

// Best effort, as the ECS logs: a line whose formatting throws is lost, and the refusal's code stands.
inline void pdkkit_disc_log_refusal(const char *step, int64_t code)
{
    try {
        RV_LOG_ERR(pdkkit_disc_tag, "{} refused the start, rv_err {}", step, code);
    } catch (...) {
        // the line is lost
    }
}

RV_MPPC_DISC_CPP_DEF(pdkkit_disc_base_)

class pdkkit_disc : public pdkkit_disc_base_
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_code = pdkkit_disc_base_::disc_initialize(pdk);
        if (base_code != RV_OK) {
            pdkkit_disc_log_refusal("the disc base", base_code);
            return base_code;
        }
        const int builtin_code = ecs_builtins_register(world_);
        if (builtin_code != RV_OK) {
            pdkkit_disc_log_refusal("the built-in components", builtin_code);
            return builtin_code;
        }
        return RV_OK;
    }

    void frame_update(float dt)
    {
        pdkkit_disc_base_::frame_update(dt);
        input_.state = rv_cio_iport_state(rv_pdko_cio(pdk_), pdkkit_input_port);
        input_.pressed = input_.state.buttons & ~previous_buttons_;
        previous_buttons_ = input_.state.buttons;
        const int64_t tick = dt > 0.0f ? 1 : 0;
        frame_ += tick;
    }

protected:
    // Loads the scene under the world root; the load number is what the disc keeps to unload it.
    int scene_load(const char *name, uint32_t &load_number)
    {
        return ecs_scene_load(world_, rv_pdko_cd(pdk_), name, ecs_id{}, load_number);
    }

    int world_frame()
    {
        return ecs_world_frame(world_, ecs_frame{ pdk_, frame_, input_ });
    }

    ecs_world world_;
    int64_t frame_ = 0;
    ecs_input input_{};

private:
    uint64_t previous_buttons_ = 0;
};

} // namespace rv_pdkkit
