// RV_MPPC_DISC_LUA_DEF writes the disc whose six hooks are a Lua chunk's six
// hooks. It is a convenience and nothing else: the console's contract is rv_de
// (pdk/de/rv_de.h), a disc may satisfy it in whatever language its author
// likes, and this header is only the shortest way to satisfy it with Lua.
//
// The body is the sequence rv_cl and the console's script chip
// (src/rv_pconsole/cl/rv_pccl_luajit.cpp) agree on: raise the entry chunk and
// check it, push the organizer before every call into it, call each hook by
// its contract name with its contract arity, read frame_update's boolean back
// and drop it, flush the frame after a frame_render that worked, and free the
// chunk on shutdown.
//
// It defines the class ONLY, so the mandatory line stays written in the disc's
// own source rather than hidden inside a macro:
//
//   RV_MPPC_DISC_LUA_DEF("my-game")
//   RV_MPPC_DISC_ENTRY_DEF(rv_dscript_disc_)
//
// title_literal is an ordinary string literal, nothing more. A disc that wants
// a title computed at runtime writes its own class instead - disc_title() is
// one line.
#pragma once

#include "pdk/cl/rv_cl.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

#define RV_MPPC_DISC_LUA_DEF(title_literal)                                              \
    class rv_dscript_disc_                                                               \
    {                                                                                    \
    public:                                                                              \
        int64_t disc_initialize(rv_pdko *pdk)                                            \
        {                                                                                \
            pdk_ = pdk;                                                                  \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            chunk_ = rv_cl_script_entry(cl);                                             \
            if (chunk_ < 0) {                                                            \
                return chunk_;                                                           \
            }                                                                            \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            const int64_t call = rv_cl_script_call(cl, chunk_, "disc_initialize", 1, 0); \
            if (call < 0) {                                                              \
                return call;                                                             \
            }                                                                            \
            return 0;                                                                    \
        }                                                                                \
        void frame_update(float dt)                                                      \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_number(cl, dt);                                             \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            const int64_t call = rv_cl_script_call(cl, chunk_, "frame_update", 2, 1);    \
            if (call < 0) {                                                              \
                return;                                                                  \
            }                                                                            \
            rv_cl_value_boolean(cl, -1, &release_);                                      \
            rv_cl_stack_drop(cl, 1);                                                     \
        }                                                                                \
        void frame_render()                                                              \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            if (rv_cl_script_call(cl, chunk_, "frame_render", 1, 0) < 0) {               \
                return;                                                                  \
            }                                                                            \
            rv_cv_frame_flush(rv_pdko_cv(pdk_));                                         \
        }                                                                                \
        bool disc_release() const                                                        \
        {                                                                                \
            return release_;                                                             \
        }                                                                                \
        void disc_shutdown()                                                             \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            rv_cl_script_call(cl, chunk_, "disc_shutdown", 1, 0);                        \
            rv_cl_script_free(cl, chunk_);                                               \
        }                                                                                \
        const char *disc_title() const                                                   \
        {                                                                                \
            return title_literal;                                                        \
        }                                                                                \
                                                                                         \
    private:                                                                             \
        rv_pdko *pdk_ = nullptr;                                                         \
        int64_t chunk_ = -1;                                                             \
        bool release_ = false;                                                           \
    };
