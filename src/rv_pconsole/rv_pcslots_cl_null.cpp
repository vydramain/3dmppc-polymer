// The cl slot table for a console built without LuaJIT (3DMPPC_LUAJIT=OFF): the
// null row only. A disc asking for luajit with script memory is refused by
// rv_pboot_check; with none, rv_pccl_resolve maps it to null first.
#include "rv_pcslots.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"

namespace rv_3dmppc
{

namespace
{

constexpr rv_pcslots_row<rv_pccl_impl> RV_PCSLOTS_CL_ROWS[] = {
    { "null", rv_pccl_impl::null, &rv_pccl_null::evaluate },
};

} // namespace

const std::span<const rv_pcslots_row<rv_pccl_impl>> RV_PCSLOTS_CL = RV_PCSLOTS_CL_ROWS;

std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &, rv_pccd &)
{
    RV_LOG_INFO("pccl", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(rv_pccl_impl::null));
    return std::make_unique<rv_pccl_null>();
}

} // namespace rv_3dmppc
