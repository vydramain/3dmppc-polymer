// The factory: the ONLY place a slot's choice is branched on. Every caller
// above this file asks for a base (rv_pcca/rv_pccv/rv_pccio/rv_pccl/rv_pccd/
// rv_pccm) and gets back whichever concrete class the choice and the
// machine's state resolve to; no `if (disabled)` belongs anywhere else in the
// tree.
//
// There is deliberately no common base over the slots — it would buy nothing
// and would invite holding them in a container. Teardown order instead relies
// on rv_pconsole's NAMED unique_ptr members: cl_ is declared last so it dies
// first, because a lua finaliser may call back through the FFI into a
// controller that must still exist.
//
// Each factory logs one line per slot: `requested X, got Y`, plus the reason
// in parentheses when they differ. X and Y come from the tables below, the
// same words --mode_<slot> and [mode.*] accept.
#pragma once

#include <memory>

#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pchost_sdl3;

// Implementation names, one row per enum value.
template <typename Impl>
struct rv_pcslots_row {
    const char *name;
    Impl impl;
};

inline constexpr rv_pcslots_row<rv_pcca_impl> RV_PCSLOTS_CA[] = {
    { "null", rv_pcca_impl::null },
    { "sdl3", rv_pcca_impl::sdl3 },
};
inline constexpr rv_pcslots_row<rv_pccv_impl> RV_PCSLOTS_CV[] = {
    { "null", rv_pccv_impl::null },
    { "sdl3", rv_pccv_impl::sdl3 },
};
inline constexpr rv_pcslots_row<rv_pccio_impl> RV_PCSLOTS_CIO[] = {
    { "null", rv_pccio_impl::null },
    { "sdl3", rv_pccio_impl::sdl3 },
};
inline constexpr rv_pcslots_row<rv_pccl_impl> RV_PCSLOTS_CL[] = {
    { "null", rv_pccl_impl::null },
    { "luajit", rv_pccl_impl::luajit },
};
inline constexpr rv_pcslots_row<rv_pccd_impl> RV_PCSLOTS_CD[] = {
    { "null", rv_pccd_impl::null },
    { "fs", rv_pccd_impl::fs },
};
inline constexpr rv_pcslots_row<rv_pccm_impl> RV_PCSLOTS_CM[] = {
    { "null", rv_pccm_impl::null },
    { "posix", rv_pccm_impl::posix },
};

const char *rv_pcslots_name(rv_pcca_impl impl);
const char *rv_pcslots_name(rv_pccv_impl impl);
const char *rv_pcslots_name(rv_pccio_impl impl);
const char *rv_pcslots_name(rv_pccl_impl impl);
const char *rv_pcslots_name(rv_pccd_impl impl);
const char *rv_pcslots_name(rv_pccm_impl impl);

// sdl3 AND host.sounding() -> rv_pcca_sdl3; sdl3 without a device ->
// rv_pcca_null, logged as a warning with the reason. null -> rv_pcca_null.
std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf, rv_pchost_sdl3 &host);

// Straight mapping.
std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf, rv_pchost_sdl3 &host);
std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pchost_sdl3 &host);
std::unique_ptr<rv_pccd> rv_pccd_make(rv_pccd_impl impl, const rv_pccd_conf &conf);
std::unique_ptr<rv_pccm> rv_pccm_make(rv_pccm_impl impl, const rv_pccm_conf &conf);

// luajit AND conf.script_memory_size > 0 -> rv_pccl_luajit; otherwise
// rv_pccl_null. An absent [budget.pccl] selects null regardless of the
// preset; the disc refuses for itself when it needed scripts and did not
// get them.
std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd);

} // namespace rv_3dmppc
