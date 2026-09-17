// The rv_cd contract made abstract. Which drive answers a name is the
// console's construction-time choice (rv_pccd_fs or rv_pccd_null).
#pragma once

#include <cstdint>
#include <memory>

#include "rv_pconsole/cd/rv_pcmedium.hpp"

namespace rv_3dmppc
{

class rv_pccd
{
public:
    virtual ~rv_pccd() = default;

    rv_pccd(const rv_pccd &) = delete;
    rv_pccd &operator=(const rv_pccd &) = delete;

    virtual int64_t asset_open(const char *resname) = 0;

    virtual int64_t asset_size(int64_t handle) = 0;

    virtual int64_t asset_read(int64_t handle, void *baddr, int64_t baddr_size) = 0;

    virtual int64_t texture_acquire(const char *resname) = 0;

    virtual int64_t texture_release(int64_t res) = 0;

    virtual int64_t texture_addr(int64_t res) = 0;

    virtual int64_t texture_palette_addr(int64_t res) = 0;

    virtual int64_t texture_width(int64_t res) = 0;

    virtual int64_t texture_height(int64_t res) = 0;

    // Console-side only - neither is reached through the extern "C" block.
    virtual void medium_insert(std::unique_ptr<rv_pcmedium> medium) = 0;

    virtual bool valid() const = 0;

protected:
    rv_pccd() = default;
};

} // namespace rv_3dmppc
