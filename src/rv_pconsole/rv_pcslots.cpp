#include "rv_pcslots.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcca_null.hpp"
#include "rv_pconsole/ca/rv_pcca_sw.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"
#include "rv_pconsole/cd/rv_pccd_null.hpp"
#include "rv_pconsole/cio/rv_pccio_null.hpp"
#include "rv_pconsole/cio/rv_pccio_std.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"
#include "rv_pconsole/cl/rv_pccl_null.hpp"
#include "rv_pconsole/cm/rv_pccm_null.hpp"
#include "rv_pconsole/cm/rv_pccm_posix.hpp"
#include "rv_pconsole/cv/rv_pccv_null.hpp"
#include "rv_pconsole/cv/rv_pccv_sw.hpp"
#include "rv_pconsole/platform/null/rv_pcplatform_null.hpp"
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3.hpp"

namespace rv_3dmppc
{

namespace
{

constexpr rv_pcslots_row<rv_pcca_impl> RV_PCSLOTS_CA_ROWS[] = {
    { "null", rv_pcca_impl::null, &rv_pcca_null::evaluate },
    { "sw", rv_pcca_impl::sw, &rv_pcca_sw::evaluate },
};
constexpr rv_pcslots_row<rv_pccv_impl> RV_PCSLOTS_CV_ROWS[] = {
    { "null", rv_pccv_impl::null, &rv_pccv_null::evaluate },
    { "sw", rv_pccv_impl::sw, &rv_pccv_sw::evaluate },
};
constexpr rv_pcslots_row<rv_pccio_impl> RV_PCSLOTS_CIO_ROWS[] = {
    { "null", rv_pccio_impl::null, &rv_pccio_null::evaluate },
    { "standard", rv_pccio_impl::standard, &rv_pccio_std::evaluate },
};
constexpr rv_pcslots_row<rv_pccl_impl> RV_PCSLOTS_CL_ROWS[] = {
    { "null", rv_pccl_impl::null, &rv_pccl_null::evaluate },
    { "luajit", rv_pccl_impl::luajit, &rv_pccl_luajit::evaluate },
};
constexpr rv_pcslots_row<rv_pccd_impl> RV_PCSLOTS_CD_ROWS[] = {
    { "null", rv_pccd_impl::null, &rv_pccd_null::evaluate },
    { "fs", rv_pccd_impl::fs, &rv_pccd_fs::evaluate },
};
constexpr rv_pcslots_row<rv_pccm_impl> RV_PCSLOTS_CM_ROWS[] = {
    { "null", rv_pccm_impl::null, &rv_pccm_null::evaluate },
    { "posix", rv_pccm_impl::posix, &rv_pccm_posix::evaluate },
};

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

const std::span<const rv_pcslots_row<rv_pcca_impl>> RV_PCSLOTS_CA = RV_PCSLOTS_CA_ROWS;
const std::span<const rv_pcslots_row<rv_pccv_impl>> RV_PCSLOTS_CV = RV_PCSLOTS_CV_ROWS;
const std::span<const rv_pcslots_row<rv_pccio_impl>> RV_PCSLOTS_CIO = RV_PCSLOTS_CIO_ROWS;
const std::span<const rv_pcslots_row<rv_pccl_impl>> RV_PCSLOTS_CL = RV_PCSLOTS_CL_ROWS;
const std::span<const rv_pcslots_row<rv_pccd_impl>> RV_PCSLOTS_CD = RV_PCSLOTS_CD_ROWS;
const std::span<const rv_pcslots_row<rv_pccm_impl>> RV_PCSLOTS_CM = RV_PCSLOTS_CM_ROWS;

const char *rv_pcslots_name(rv_pcca_impl impl)
{
    return impl_name(RV_PCSLOTS_CA, impl);
}
const char *rv_pcslots_name(rv_pccv_impl impl)
{
    return impl_name(RV_PCSLOTS_CV, impl);
}
const char *rv_pcslots_name(rv_pccio_impl impl)
{
    return impl_name(RV_PCSLOTS_CIO, impl);
}
const char *rv_pcslots_name(rv_pccl_impl impl)
{
    return impl_name(RV_PCSLOTS_CL, impl);
}
const char *rv_pcslots_name(rv_pccd_impl impl)
{
    return impl_name(RV_PCSLOTS_CD, impl);
}
const char *rv_pcslots_name(rv_pccm_impl impl)
{
    return impl_name(RV_PCSLOTS_CM, impl);
}
const char *rv_pcslots_name(rv_pcplatform_impl impl)
{
    return impl_name(RV_PCSLOTS_PLATFORM, impl);
}

std::unique_ptr<rv_pcca> rv_pcca_make(rv_pcca_impl impl, const rv_pcca_conf &conf)
{
    RV_LOG_INFO("pcca", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pcca_impl::sw) {
        return std::make_unique<rv_pcca_sw>(conf);
    }
    return std::make_unique<rv_pcca_null>(conf);
}

std::unique_ptr<rv_pccv> rv_pccv_make(rv_pccv_impl impl, const rv_pccv_conf &conf)
{
    RV_LOG_INFO("pccv", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccv_impl::sw) {
        return std::make_unique<rv_pccv_sw>(conf);
    }
    return std::make_unique<rv_pccv_null>(conf);
}

std::unique_ptr<rv_pccio> rv_pccio_make(rv_pccio_impl impl, const rv_pccio_conf &conf, rv_pcplatform &platform)
{
    RV_LOG_INFO("pccio", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pccio_impl::standard) {
        return std::make_unique<rv_pccio_std>(conf, platform.window(), platform.gamepads());
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

std::unique_ptr<rv_pcplatform> rv_pcplatform_make(rv_pcplatform_impl impl, const rv_pcplatform_wants &wants)
{
    RV_LOG_INFO("pcplatform", "requested {}, got {}", rv_pcslots_name(impl), rv_pcslots_name(impl));
    if (impl == rv_pcplatform_impl::sdl3) {
        return rv_pcplatform_sdl3_make(wants);
    }
    return rv_pcplatform_null_make(wants);
}

} // namespace rv_3dmppc
