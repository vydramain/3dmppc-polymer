// The cl slot table (null and luajit rows, RV_PCSLOTS_CL) and the rv_pccl_make
// factory. rv_pcslots.cpp keeps the tables of the other slots and the
// rv_pcslots_name / rv_pccl_resolve functions.
#include "rv_pcslots.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"

namespace rv_3dmppc
{

namespace
{

constexpr rv_pcslots_row<rv_pccl_impl> RV_PCSLOTS_CL_ROWS[] = {
    { "null", rv_pccl_impl::null, &rv_pccl_null::evaluate },
    { "luajit", rv_pccl_impl::luajit, &rv_pccl_luajit::evaluate },
};

} // namespace

const std::span<const rv_pcslots_row<rv_pccl_impl>> RV_PCSLOTS_CL = RV_PCSLOTS_CL_ROWS;

std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd)
{
    RV_LOG_INFO("pccl", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccl_impl::luajit) {
        return std::make_unique<rv_pccl_luajit>(conf, cd);
    }
    return std::make_unique<rv_pccl_null>();
}

} // namespace rv_3dmppc
