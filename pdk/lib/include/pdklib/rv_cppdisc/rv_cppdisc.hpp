// RV_MPPC_DISC_CPP_DEF is the C++ half of the same convenience
// RV_MPPC_DISC_LUA_DEF (pdklib/rv_dscript/rv_dscript.hpp) gives a disc that
// hands its hooks to a Lua chunk. There is no pdk-side contract to lean on
// here the way rv_dv.h's RV_MPPC_DISC_CL_BASE_DEF is one for the Lua case: a
// C++ disc's class already IS its own hooks, so pdk's whole contract is
// "write disc_initialize/frame_update/frame_render/disc_release/
// disc_shutdown/disc_title and hand the class to RV_MPPC_DISC_DEF" - nothing
// about that needs a base class to exist. What repeats from one C++ disc to
// the next instead, and what this header exists to stop retyping, is:
//
//   - disc_initialize's opening guards. Every disc that draws anything and
//     reads a gamepad needs the GPU and the input controller to exist, a
//     screen worth drawing to, room in the frame for more than a couple of
//     primitives, and at least one port to read - none of that is a choice
//     any particular game makes, it is what "this disc can run at all"
//     means, so checking it once here is what saves every disc from
//     repeating the same four guards before it can start on its own
//     content.
//
//   - frame_update's press-edge tracking. rv_cio only ever reports whether a
//     button is CURRENTLY held; turning that into "the player just pressed
//     it" - which is what a MENU-button release action wants, so holding it
//     down does not fire every frame - takes comparing this frame's mask
//     against the last one, and that comparison is the same for any disc
//     that wants edge-triggered input, not something this game's content
//     decides.
//
//   - read_asset(). Opening, sizing and reading an asset is the same
//     three-call dance and the same "any of these can fail" handling
//     regardless of which asset or which game - the asset's NAME and what
//     the bytes mean once read are the only things that differ disc to
//     disc, and those stay the caller's.
//
// The trailing rv_cv_frame_flush() in frame_render is NOT here: it is one
// unconditional call with no logic of its own, already as short written
// inline as it would be behind a name, and wrapping it would need either a
// virtual call or a CRTP hook to let a base class run code after a derived
// class's own frame_render body - machinery this single call does not earn.
// It stays where every disc's own frame_render ends.
//
// class_name is an ordinary identifier, nothing more - the same rule
// RV_MPPC_DISC_CL_BASE_DEF documents applies here. The macro defines the class
// ONLY; a disc using it derives its own class from class_name, adds
// whatever is its own content, and plants the FINAL class with
// RV_MPPC_DISC_DEF, exactly as example-cpp.cpp does:
//
//   RV_MPPC_DISC_CPP_DEF(rv_dmain_base_)
//   class rv_dmain : public rv_dmain_base_ { ... };
//   RV_MPPC_DISC_DEF(rv_dmain)
//
// A disc's own overrides of disc_initialize/frame_update call the base
// class's version first (rv_dmain_base_::disc_initialize(pdk), etc.) the
// same way RV_MPPC_DISC_LUA_DEF's derived frame_render calls
// rv_dscript_disc_base_::frame_render before deciding what to do with its
// result - name hiding, not virtual dispatch, because RV_MPPC_DISC_DEF
// already knows the disc's most-derived type and calls it directly. A disc
// is free to skip this header and write all five hooks by hand, the way
// example-cpp.cpp did before this header existed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"

#define RV_MPPC_DISC_CPP_DEF(class_name)                                           \
    class class_name                                                               \
    {                                                                              \
    public:                                                                        \
        int64_t disc_initialize(rv_pdko *pdk)                                      \
        {                                                                          \
            pdk_ = pdk;                                                            \
            rv_cv *cv = rv_pdko_cv(pdk_);                                          \
            rv_cio *cio = rv_pdko_cio(pdk_);                                       \
            if (!cv || !cio) {                                                     \
                return RV_ERR_INVAL;                                               \
            }                                                                      \
            if (rv_cv_screen_width(cv) < 64 || rv_cv_screen_height(cv) < 64) {     \
                return RV_ERR_INVAL;                                               \
            }                                                                      \
            if (rv_cv_frame_capacity(cv) < 8) {                                    \
                return RV_ERR_INVAL;                                               \
            }                                                                      \
            if (rv_cio_iport_count(cio) < 1) {                                     \
                return RV_ERR_INVAL;                                               \
            }                                                                      \
            return RV_OK;                                                          \
        }                                                                          \
        void frame_update(float dt)                                                \
        {                                                                          \
            (void)dt;                                                              \
            const uint64_t now = rv_cio_iport_state(rv_pdko_cio(pdk_), 0).buttons;  \
            if ((now & ~prev_buttons_) & RV_ISOURCE_MENU_BTTN_MENU) {              \
                release_ = true;                                                   \
            }                                                                      \
            prev_buttons_ = now;                                                   \
        }                                                                          \
        bool disc_release() const                                                  \
        {                                                                          \
            return release_;                                                       \
        }                                                                          \
                                                                                     \
    protected:                                                                     \
        bool read_asset(const char *name, std::vector<uint8_t> &out)               \
        {                                                                          \
            rv_cd *cd = rv_pdko_cd(pdk_);                                          \
            if (!cd) {                                                             \
                return false;                                                      \
            }                                                                      \
            const int64_t handle = rv_cd_asset_open(cd, name);                     \
            if (handle < 0) {                                                      \
                return false;                                                      \
            }                                                                      \
            const int64_t size = rv_cd_asset_size(cd, handle);                     \
            if (size < 0) {                                                        \
                return false;                                                      \
            }                                                                      \
            out.assign(static_cast<std::size_t>(size), 0);                         \
            const int64_t read = rv_cd_asset_read(cd, handle, out.data(), size);   \
            if (read < 0) {                                                        \
                return false;                                                      \
            }                                                                      \
            out.resize(static_cast<std::size_t>(read));                            \
            return true;                                                          \
        }                                                                          \
                                                                                     \
        rv_pdko *pdk_ = nullptr;                                                   \
                                                                                     \
    private:                                                                       \
        uint64_t prev_buttons_ = 0;                                                \
        bool release_ = false;                                                     \
    };
