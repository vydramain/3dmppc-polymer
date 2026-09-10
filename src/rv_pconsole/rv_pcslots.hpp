// The factory: the ONLY place a slot's choice is branched on. Every caller
// above this file asks for a base (rv_pcca/rv_pccv/rv_pccio/rv_pccl) and gets
// back whichever concrete class the choice and the machine's state resolve
// to; no `if (disabled)` belongs anywhere else in the tree.
//
// There is deliberately no common base over the slots — it would buy nothing
// and would invite holding them in a container. Teardown order instead relies
// on rv_pconsole's NAMED unique_ptr members: cl_ is declared last so it dies
// first, because a lua finaliser may call back through the FFI into a
// controller that must still exist.
#pragma once

#include <memory>

#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pchost_sdl3;
class rv_pccd;

// sdl3 AND host.sounding() -> rv_pcca_sdl3. sdl3 without a device -> a
// warning and rv_pcca_null (today's "[no device]" behaviour). null ->
// rv_pcca_null.
std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf, rv_pchost_sdl3 &host);

// Straight mapping.
std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf, rv_pchost_sdl3 &host);
std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pchost_sdl3 &host);

// luajit AND conf.script_memory_size > 0 -> rv_pccl_luajit; otherwise
// rv_pccl_null. An absent [budget.pccl] selects null regardless of the
// preset; the disc refuses for itself when it needed scripts and did not
// get them.
std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd);

} // namespace rv_3dmppc
