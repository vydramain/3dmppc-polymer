#include "rv_pcslots.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcca_null.hpp"
#include "rv_pconsole/ca/rv_pcca_sdl3.hpp"
#include "rv_pconsole/cio/rv_pccio_null.hpp"
#include "rv_pconsole/cio/rv_pccio_sdl3.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"
#include "rv_pconsole/cv/rv_pccv_null.hpp"
#include "rv_pconsole/cv/rv_pccv_sdl3.hpp"
#include "rv_pconsole/rv_pchost_sdl3.hpp"

namespace rv_3dmppc
{

std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf, rv_pchost_sdl3 &host)
{
    if (impl == rv_pcca_impl::sdl3) {
        if (host.sounding()) {
            return std::make_unique<rv_pcca_sdl3>(conf, host);
        }
        RV_LOG_WARN("pcca", "no audio device, falling back to the null implementation");
    }
    return std::make_unique<rv_pcca_null>(conf);
}

std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf, rv_pchost_sdl3 &host)
{
    if (impl == rv_pccv_impl::sdl3) {
        return std::make_unique<rv_pccv_sdl3>(conf, host);
    }
    return std::make_unique<rv_pccv_null>(conf);
}

std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pchost_sdl3 &host)
{
    if (impl == rv_pccio_impl::sdl3) {
        return std::make_unique<rv_pccio_sdl3>(conf, host);
    }
    return std::make_unique<rv_pccio_null>(conf);
}

std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd)
{
    if (impl == rv_pccl_impl::luajit && conf.script_memory_size > 0) {
        return std::make_unique<rv_pccl_luajit>(conf, cd);
    }
    return std::make_unique<rv_pccl_null>();
}

} // namespace rv_3dmppc
