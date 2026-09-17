// Forwards the six rv_de hooks into a Lua chunk. Every Lua disc repeats the
// same thirteen-call boilerplate (see example-lua.cpp before this header
// existed) - RV_MPPC_LUA_DISC_DEF defines the disc class AND raises it via
// RV_MPPC_DISC_ENTRY_DEF (pdk/include/pdk/de/rv_dv.h), so a disc's own .cpp
// is left with nothing but the macro call.
#pragma once

#include <cstdint>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

// title_literal becomes disc_title()'s return value. The generated class
// hands pdk_ to the script on every hook (instead of individual controllers)
// so the script derives cv/ca/cio/cd itself through pdk.pdko_cv(o) etc.
//
// A failing rv_cl_script_call is never logged here: the machine has already
// logged the Lua message with the chunk name and hook name attached (see the
// contract comment on rv_cl_script_call in pdk/include/pdk/cl/rv_cl.h) - a
// second line here would only repeat it.
#define RV_MPPC_LUA_DISC_DEF(title_literal)                                                \
    class rv_dscript_disc_                                                                 \
    {                                                                                      \
    public:                                                                                \
        int64_t disc_initialize(rv_pdko *pdk)                                              \
        {                                                                                  \
            pdk_ = pdk;                                                                    \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                  \
            chunk_ = rv_cl_script_entry(cl);                                                \
            if (chunk_ < 0) {                                                              \
                return chunk_;                                                             \
            }                                                                              \
            rv_cl_stack_push_pointer(cl, pdk_);                                            \
            const int64_t call = rv_cl_script_call(cl, chunk_, "disc_initialize", 1, 0);   \
            if (call < 0) {                                                                \
                return call;                                                               \
            }                                                                              \
            return 0;                                                                     \
        }                                                                                  \
        void frame_update(float dt)                                                        \
        {                                                                                  \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                  \
            rv_cl_stack_push_number(cl, dt);                                                \
            rv_cl_stack_push_pointer(cl, pdk_);                                            \
            const int64_t call = rv_cl_script_call(cl, chunk_, "frame_update", 2, 1);      \
            if (call < 0) {                                                                \
                return;                                                                    \
            }                                                                              \
            rv_cl_value_boolean(cl, -1, &release_);                                         \
            rv_cl_stack_drop(cl, 1);                                                        \
        }                                                                                  \
        void frame_render()                                                                \
        {                                                                                  \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                  \
            rv_cl_stack_push_pointer(cl, pdk_);                                            \
            const int64_t call = rv_cl_script_call(cl, chunk_, "frame_render", 1, 0);      \
            if (call < 0) {                                                                \
                return;                                                                    \
            }                                                                              \
            /* Script fills frame; C++ closes it, like example-cpp. */                      \
            rv_cv_frame_flush(rv_pdko_cv(pdk_));                                            \
        }                                                                                  \
        bool disc_release() const                                                           \
        {                                                                                  \
            return release_;                                                               \
        }                                                                                  \
        void disc_shutdown()                                                                \
        {                                                                                  \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                  \
            rv_cl_stack_push_pointer(cl, pdk_);                                            \
            rv_cl_script_call(cl, chunk_, "disc_shutdown", 1, 0);                           \
            rv_cl_script_free(cl, chunk_);                                                  \
        }                                                                                  \
        const char *disc_title() const                                                     \
        {                                                                                  \
            return title_literal;                                                          \
        }                                                                                  \
                                                                                             \
    private:                                                                               \
        rv_pdko *pdk_ = nullptr;                                                            \
        int64_t chunk_ = -1;                                                                \
        bool release_ = false;                                                              \
    };                                                                                     \
    RV_MPPC_DISC_ENTRY_DEF(rv_dscript_disc_)
