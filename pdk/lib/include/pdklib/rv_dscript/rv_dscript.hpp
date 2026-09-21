// RV_MPPC_DISC_LUA_DEF derives a class from RV_MPPC_DISC_CL_BASE_DEF's
// (pdk/include/pdk/de/rv_dv.h) instead of parameterising it, adding exactly
// the two things every Lua disc in THIS project happens to repeat and which
// the pdk contract has no business deciding: a fixed title, handed straight
// through to the base class, and flushing the frame right after
// frame_render's rv_cl_script_call succeeds - a convention this project
// follows, not something rv_cl's contract requires, which is why
// RV_MPPC_DISC_CL_BASE_DEF's own frame_render() only reports success rather
// than flushing itself. A disc built straight on RV_MPPC_DISC_CL_BASE_DEF is
// free to answer that question differently, or to skip this header
// entirely and derive its own class the same way this one does.
#pragma once

#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

#define RV_MPPC_DISC_LUA_DEF(title_literal)                        \
    RV_MPPC_DISC_CL_BASE_DEF(rv_dscript_disc_base_, title_literal) \
    class rv_dscript_disc_ : public rv_dscript_disc_base_          \
    {                                                              \
    public:                                                        \
        void frame_render()                                        \
        {                                                          \
            if (rv_dscript_disc_base_::frame_render()) {           \
                rv_cv_frame_flush(rv_pdko_cv(pdk_));               \
            }                                                      \
        }                                                          \
    };                                                             \
    RV_MPPC_DISC_DEF(rv_dscript_disc_)
