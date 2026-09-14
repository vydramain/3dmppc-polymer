// The factory: the ONLY place a slot's choice is branched on. Every caller
// above this file asks for a base (rv_pcca/rv_pccv/rv_pccio/rv_pccl/rv_pccd/
// rv_pccm) and gets back whichever concrete class the choice resolves to, and
// asks for the platform the same way; no `if (disabled)` belongs anywhere
// else in the tree.
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
#include "rv_pconsole/ca/rv_pcca_null.hpp"
#include "rv_pconsole/ca/rv_pcca_sw.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"
#include "rv_pconsole/cd/rv_pccd_null.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cio/rv_pccio_null.hpp"
#include "rv_pconsole/cio/rv_pccio_std.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cm/rv_pccm_null.hpp"
#include "rv_pconsole/cm/rv_pccm_posix.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/cv/rv_pccv_null.hpp"
#include "rv_pconsole/cv/rv_pccv_sw.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Implementation names, one row per enum value, plus the class's own budget
// evaluation (rv_pcbudget.hpp) — the one function boot stage E3 calls before
// this class is ever constructed.
template <typename Impl>
struct rv_pcslots_row {
    const char *name;
    Impl impl;
    rv_pcbudget_evaluate_fn evaluate;
};

inline constexpr rv_pcslots_row<rv_pcca_impl> RV_PCSLOTS_CA[] = {
    { "null", rv_pcca_impl::null, &rv_pcca_null::evaluate },
    { "sw", rv_pcca_impl::sw, &rv_pcca_sw::evaluate },
};
inline constexpr rv_pcslots_row<rv_pccv_impl> RV_PCSLOTS_CV[] = {
    { "null", rv_pccv_impl::null, &rv_pccv_null::evaluate },
    { "sw", rv_pccv_impl::sw, &rv_pccv_sw::evaluate },
};
inline constexpr rv_pcslots_row<rv_pccio_impl> RV_PCSLOTS_CIO[] = {
    { "null", rv_pccio_impl::null, &rv_pccio_null::evaluate },
    { "standard", rv_pccio_impl::standard, &rv_pccio_std::evaluate },
};
inline constexpr rv_pcslots_row<rv_pccl_impl> RV_PCSLOTS_CL[] = {
    { "null", rv_pccl_impl::null, &rv_pccl_null::evaluate },
    { "luajit", rv_pccl_impl::luajit, &rv_pccl_luajit::evaluate },
};
inline constexpr rv_pcslots_row<rv_pccd_impl> RV_PCSLOTS_CD[] = {
    { "null", rv_pccd_impl::null, &rv_pccd_null::evaluate },
    { "fs", rv_pccd_impl::fs, &rv_pccd_fs::evaluate },
};
inline constexpr rv_pcslots_row<rv_pccm_impl> RV_PCSLOTS_CM[] = {
    { "null", rv_pccm_impl::null, &rv_pccm_null::evaluate },
    { "posix", rv_pccm_impl::posix, &rv_pccm_posix::evaluate },
};

const char *rv_pcslots_name(rv_pcca_impl impl);
const char *rv_pcslots_name(rv_pccv_impl impl);
const char *rv_pcslots_name(rv_pccio_impl impl);
const char *rv_pcslots_name(rv_pccl_impl impl);
const char *rv_pcslots_name(rv_pccd_impl impl);
const char *rv_pcslots_name(rv_pccm_impl impl);

// The platform is not a slot and costs nothing against a budget: it is
// chosen the same way (--mode_platform, [mode.*] platform=) but never
// appears in a table above and is never evaluate()-checked.
struct rv_pcslots_platform_row {
    const char *name;
    rv_pcplatform_impl impl;
};
inline constexpr rv_pcslots_platform_row RV_PCSLOTS_PLATFORM[] = {
    { "null", rv_pcplatform_impl::null },
    { "sdl3", rv_pcplatform_impl::sdl3 },
};
const char *rv_pcslots_name(rv_pcplatform_impl impl);

// Straight mapping, no device fallback: what was asked for is what is built.
std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf);
std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf);
std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pcplatform &platform);
std::unique_ptr<rv_pccd> rv_pccd_make(rv_pccd_impl impl, const rv_pccd_conf &conf);
std::unique_ptr<rv_pccm> rv_pccm_make(rv_pccm_impl impl, const rv_pccm_conf &conf);

// luajit AND conf.script_memory_size > 0 -> rv_pccl_luajit; otherwise
// rv_pccl_null. An absent [budget.pccl] selects null regardless of the
// preset; the disc refuses for itself when it needed scripts and did not
// get them.
std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd);

// Straight mapping to rv_pcplatform_sdl3_make / rv_pcplatform_null_make,
// logged the same way as every other factory ("requested X, got X" under
// "pcplatform").
std::unique_ptr<rv_pcplatform> rv_pcplatform_make(rv_pcplatform_impl impl, const rv_pcplatform_wants &wants);

} // namespace rv_3dmppc
