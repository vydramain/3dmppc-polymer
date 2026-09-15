// The factory: the ONLY place a slot's choice is branched on. Every caller
// above this file asks for a base (rv_pcca/rv_pccv/rv_pccio/rv_pccl/rv_pccd/
// rv_pccm) and gets back whichever concrete class the choice resolves to, and
// asks for the platform the same way; no `if (disabled)` belongs anywhere
// else in the tree.
//
// There is deliberately no common base over the slots - it would buy nothing
// and would invite holding them in a container. Teardown order instead relies
// on rv_pconsole's NAMED unique_ptr members: cl_ is declared last so it dies
// first, because a lua finaliser may call back through the FFI into a
// controller that must still exist.
//
// Each factory logs one line per slot: `requested X, got Y`, plus the reason
// in parentheses when they differ. X and Y come from the tables below, the
// same words --mode_<slot> and [mode.*] accept.
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Implementation names, one row per enum value, plus the class's own budget
// evaluation (rv_pcbudget.hpp), called by boot before the class is ever
// constructed. Defined in rv_pcslots.cpp so no caller sees a concrete class.
template <typename Impl>
struct rv_pcslots_row {
    const char *name;
    Impl impl;
    rv_pcbudget_evaluate_fn evaluate;
};

extern const std::span<const rv_pcslots_row<rv_pcca_impl>> RV_PCSLOTS_CA;
extern const std::span<const rv_pcslots_row<rv_pccv_impl>> RV_PCSLOTS_CV;
extern const std::span<const rv_pcslots_row<rv_pccio_impl>> RV_PCSLOTS_CIO;
extern const std::span<const rv_pcslots_row<rv_pccl_impl>> RV_PCSLOTS_CL;
extern const std::span<const rv_pcslots_row<rv_pccd_impl>> RV_PCSLOTS_CD;
extern const std::span<const rv_pcslots_row<rv_pccm_impl>> RV_PCSLOTS_CM;

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

// Downgrades luajit to null when the disc declares no script memory. The
// caller must call this before both budget evaluation and rv_pccl_make, so
// the two see the same implementation.
rv_pccl_impl rv_pccl_resolve(rv_pccl_impl requested, int64_t script_memory_size);

// Straight mapping, same as the factories above: what rv_pccl_resolve
// returned is what is built. The caller resolves first.
std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd);

// Straight mapping to rv_pcplatform_sdl3_make / rv_pcplatform_null_make,
// logged the same way as every other factory ("requested X, got X" under
// "pcplatform").
std::unique_ptr<rv_pcplatform> rv_pcplatform_make(rv_pcplatform_impl impl, const rv_pcplatform_wants &wants);

} // namespace rv_3dmppc
