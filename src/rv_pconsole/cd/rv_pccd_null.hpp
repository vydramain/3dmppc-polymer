// The rv_cd contract with no drive behind it: every name is absent and no
// handle is ever issued.
#pragma once

#include <cstdint>
#include <memory>

#include "pdk/rv_err.h"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"

namespace rv_3dmppc
{

class rv_pccd_null final : public rv_pccd
{
public:
    rv_pccd_null() = default;

    // No drive: RV_OK, bytes = 0.
    static int evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

    int64_t asset_open(const char *resname) override;

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void *baddr, int64_t baddr_size) override;

    int64_t resource_addr(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_size(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_palette_addr(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_width(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_height(rv_cd_resource_kind kind, const char *resname) override;

    int64_t asset_reload(const char *resname, rv_cd_resource_kind &kind_out) override;

    int64_t stream_play(const char *resname, const rv_cd_stream_conf *conf) override;

    int64_t stream_stop() override;

    int64_t stream_status(rv_cd_stream_info *out) override;

    int64_t stream_pump() override;

    // No drive to put it in: the medium is dropped.
    void medium_insert(std::unique_ptr<rv_pcmedium> /*medium*/) override
    {
    }

    // No drive, so nothing is ever made resident: ignored.
    void video_attach(rv_pccv & /*cv*/) override
    {
    }
    void audio_attach(rv_pcca & /*ca*/) override
    {
    }
    void geometry_attach(rv_pccg & /*cg*/) override
    {
    }

    bool valid() const override
    {
        return true;
    }
};

} // namespace rv_3dmppc
