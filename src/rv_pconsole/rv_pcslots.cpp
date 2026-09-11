#include "rv_pcslots.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcca_null.hpp"
#include "rv_pconsole/ca/rv_pcca_sdl3.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"
#include "rv_pconsole/cd/rv_pccd_null.hpp"
#include "rv_pconsole/cio/rv_pccio_null.hpp"
#include "rv_pconsole/cio/rv_pccio_sdl3.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"
#include "rv_pconsole/cm/rv_pccm_null.hpp"
#include "rv_pconsole/cm/rv_pccm_posix.hpp"
#include "rv_pconsole/cv/rv_pccv_null.hpp"
#include "rv_pconsole/cv/rv_pccv_sdl3.hpp"
#include "rv_pconsole/rv_pchost_sdl3.hpp"

namespace rv_3dmppc
{

namespace
{

template <typename Table, typename Impl>
const char *impl_name(const Table &table, Impl impl)
{
    for (const auto &row : table) {
        if (row.impl == impl) {
            return row.name;
        }
    }
    return "null";
}

} // namespace

const char *rv_pcslots_name(rv_pcca_impl impl)
{
    return impl_name(kPcca, impl);
}
const char *rv_pcslots_name(rv_pccv_impl impl)
{
    return impl_name(kPccv, impl);
}
const char *rv_pcslots_name(rv_pccio_impl impl)
{
    return impl_name(kPccio, impl);
}
const char *rv_pcslots_name(rv_pccl_impl impl)
{
    return impl_name(kPccl, impl);
}
const char *rv_pcslots_name(rv_pccd_impl impl)
{
    return impl_name(kPccd, impl);
}
const char *rv_pcslots_name(rv_pccm_impl impl)
{
    return impl_name(kPccm, impl);
}

std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf, rv_pchost_sdl3 &host)
{
    if (impl == rv_pcca_impl::sdl3) {
        if (host.sounding()) {
            RV_LOG_INFO("pcca", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(rv_pcca_impl::sdl3));
            return std::make_unique<rv_pcca_sdl3>(conf, host);
        }
        RV_LOG_WARN("pcca", "requested {}, got {} (no audio device)", rv_pcslots_name(impl),
            rv_pcslots_name(rv_pcca_impl::null));
        return std::make_unique<rv_pcca_null>(conf);
    }
    RV_LOG_INFO("pcca", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(rv_pcca_impl::null));
    return std::make_unique<rv_pcca_null>(conf);
}

std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf, rv_pchost_sdl3 &host)
{
    RV_LOG_INFO("pccv", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccv_impl::sdl3) {
        return std::make_unique<rv_pccv_sdl3>(conf, host);
    }
    return std::make_unique<rv_pccv_null>(conf);
}

std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pchost_sdl3 &host)
{
    RV_LOG_INFO("pccio", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccio_impl::sdl3) {
        return std::make_unique<rv_pccio_sdl3>(conf, host);
    }
    return std::make_unique<rv_pccio_null>(conf);
}

std::unique_ptr<rv_pccl> rv_pccl_make(rv_pccl_impl impl, const rv_pccl_conf &conf, rv_pccd &cd)
{
    if (impl == rv_pccl_impl::luajit && conf.script_memory_size > 0) {
        RV_LOG_INFO("pccl", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(rv_pccl_impl::luajit));
        return std::make_unique<rv_pccl_luajit>(conf, cd);
    }
    if (impl == rv_pccl_impl::luajit) {
        RV_LOG_INFO("pccl", "requested {}, got {} (the disc declares no script memory)", rv_pcslots_name(impl),
            rv_pcslots_name(rv_pccl_impl::null));
    } else {
        RV_LOG_INFO("pccl", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(rv_pccl_impl::null));
    }
    return std::make_unique<rv_pccl_null>();
}

std::unique_ptr<rv_pccd> rv_pccd_make(rv_pccd_impl impl, const rv_pccd_conf &conf)
{
    RV_LOG_INFO("pccd", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccd_impl::fs) {
        return std::make_unique<rv_pccd_fs>(conf);
    }
    return std::make_unique<rv_pccd_null>();
}

std::unique_ptr<rv_pccm> rv_pccm_make(rv_pccm_impl impl, const rv_pccm_conf &conf)
{
    RV_LOG_INFO("pccm", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccm_impl::posix) {
        return std::make_unique<rv_pccm_posix>(conf);
    }
    return std::make_unique<rv_pccm_null>(conf);
}

} // namespace rv_3dmppc
