// The console's rv_cd implementation: the DRIVE, not the disc. It owns the
// contract's semantics - legal names, stable handles, which rv_err a situation
// deserves - and delegates every actual byte to an rv_pcmedium (rv_pcmedium.hpp),
// so it has no idea whether the disc it is reading is a directory
// (rv_pcdirmedium) or a `.mppcdisc` archive (rv_pczipmedium).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cd/rv_pcmedium.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {
class rv_pccd_fs final : public rv_pccd {
   private:
    rv_pccd_conf conf_;

    // What is inserted. Held by pointer purely so the medium KIND can change
    // without this class changing (strategy); it is never null, an
    // empty drive is a mounted-less medium rather than a missing one.
    std::unique_ptr<rv_pcmedium> medium_;

    // Handle table - a handle is simply an index into `resnames_`, and
    // `by_name_` makes the resolution idempotent. Two properties fall out, and
    // both are contract requirements rather than conveniences:
    //   * the same name always yields the same handle, because a name resolved
    //     once is found in `by_name_` and its index returned again;
    //   * a handle needs no release and can never dangle, because the table only
    //     ever grows - nothing is erased, reordered or reused for a later name.
    // The cost is one std::string per DISTINCT name ever opened, which is
    // bounded by the disc's asset set and is why no eviction policy is needed.
    std::vector<std::string> resnames_;
    std::unordered_map<std::string, int64_t> by_name_;

   public:
    explicit rv_pccd_fs(const rv_pccd_conf& conf);
    ~rv_pccd_fs() = default;

    rv_pccd_fs(const rv_pccd_fs&) = delete;
    rv_pccd_fs& operator=(const rv_pccd_fs&) = delete;

    // The drive reads assets out of the disc's own archive, not out of a
    // budgeted pool: always 0 bytes.
    static rv_pcbudget_cost evaluate(const rv_pdklib::rv_manifest_budget& budget);

    int64_t asset_open(const char* resname) override;

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void* baddr, int64_t baddr_size) override;

    // Working implementation lands in the next slice; today these are inert.
    int64_t texture_acquire(const char* resname) override;

    int64_t texture_release(int64_t res) override;

    int64_t texture_addr(int64_t res) override;

    int64_t texture_palette_addr(int64_t res) override;

    int64_t texture_width(int64_t res) override;

    int64_t texture_height(int64_t res) override;

    // Swap the inserted medium after construction. The console learns
    // WHICH archive to mount only when it has loaded the disc out of it, which
    // is later than this object is built; the conf-built directory medium (the
    // catalogue path) is untouched and stays the default. Strategy -
    // this is the one seam where the strategy is chosen, and it is deliberately
    // the only one.
    void medium_insert(std::unique_ptr<rv_pcmedium> medium) override {
        if (medium) medium_ = std::move(medium);
    }

    // An empty or unmountable medium leaves the drive empty, never broken.
    bool valid() const override { return true; }

   private:
    // The upper bound on distinct names one disc may resolve. It exists so that
    // RV_ERR_NOMEM is a real, testable answer ("the resource table cannot grow")
    // instead of a code that only ever appears when the host is already dying.
    static constexpr int64_t RV_PCCD_FS_RESOURCE_TABLE_MAX = 4096;

    // Name behind a handle, or nullptr when the handle was never issued.
    const char* handle_name(int64_t handle) const;
};

}  // namespace rv_3dmppc
