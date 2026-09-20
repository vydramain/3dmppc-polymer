// The console's rv_cd implementation: the DRIVE, not the disc. It owns the
// contract's semantics - legal names, stable handles, which rv_err a situation
// deserves - and delegates every actual byte to an rv_pcmedium (rv_pcmedium.hpp),
// so it has no idea whether the disc it is reading is a directory
// (rv_pcdirmedium) or a `.mppcdisc` archive (rv_pczipmedium).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pdklib/rv_textures/rv_mppctex.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cd/rv_pcmedium.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
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

    // BORROWED, set by video_attach() after construction (rv_pccd.hpp: cd_ is
    // built before cv_ exists, so this cannot be a constructor reference).
    // Null until attached, which the four rv_cd_resource_* functions (addr,
    // palette_addr, width, height) treat the same way asset_open treats an
    // unmounted medium: not an error, just nothing resident yet.
    rv_pccv *cv_ = nullptr;

    // One baked texture currently uploaded, keyed by name in `tex_by_name_`.
    // A disc never releases a texture, so the vector only ever grows and a
    // record lives for as long as the drive does - freed only in ~rv_pccd_fs,
    // never one at a time.
    struct texture_record {
        std::string resname;
        int64_t tex_addr = 0;
        int64_t pal_addr = 0; // 0 when the format has no palette
        int64_t width = 0;
        int64_t height = 0;
    };
    std::vector<texture_record> textures_;
    std::unordered_map<std::string, int64_t> tex_by_name_;

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
    // The drive's teardown: a disc never releases a texture, so this is the
    // only place any of them are freed - everything the drive ever made
    // resident for this disc goes back to video RAM here, all at once.
    ~rv_pccd_fs() override;

    rv_pccd_fs(const rv_pccd_fs&) = delete;
    rv_pccd_fs& operator=(const rv_pccd_fs&) = delete;

    // The drive reads assets out of the disc's own archive, not out of a
    // budgeted pool: always 0 bytes.
    static rv_pcbudget_cost evaluate(const rv_pdklib::rv_manifest_budget& budget);

    int64_t asset_open(const char* resname) override;

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void* baddr, int64_t baddr_size) override;

    int64_t resource_addr(rv_cd_resource_kind kind, const char* resname) override;

    int64_t resource_palette_addr(rv_cd_resource_kind kind, const char* resname) override;

    int64_t resource_width(rv_cd_resource_kind kind, const char* resname) override;

    int64_t resource_height(rv_cd_resource_kind kind, const char* resname) override;

    int64_t texture_reload(const char* resname) override;

    // Swap the inserted medium after construction. The console learns
    // WHICH archive to mount only when it has loaded the disc out of it, which
    // is later than this object is built; the conf-built directory medium (the
    // catalogue path) is untouched and stays the default. Strategy -
    // this is the one seam where the strategy is chosen, and it is deliberately
    // the only one.
    void medium_insert(std::unique_ptr<rv_pcmedium> medium) override {
        if (medium) medium_ = std::move(medium);
    }

    void video_attach(rv_pccv& cv) override { cv_ = &cv; }

    // An empty or unmountable medium leaves the drive empty, never broken.
    bool valid() const override { return true; }

   private:
    // The upper bound on distinct names one disc may resolve. It exists so that
    // RV_ERR_NOMEM is a real, testable answer ("the resource table cannot grow")
    // instead of a code that only ever appears when the host is already dying.
    static constexpr int64_t RV_PCCD_FS_RESOURCE_TABLE_MAX = 4096;

    // Name behind a handle, or nullptr when the handle was never issued.
    const char* handle_name(int64_t handle) const;

    // The record behind `resname`: a cache hit returns the existing upload,
    // a cache miss reads, decodes and uploads it - this is where a disc's
    // "first ask" becomes resident. `record_out` is set only on success;
    // the return is RV_OK or the negative rv_err reading, decoding or
    // uploading answered (the same rv_err asset_open would give the name,
    // when that is where it failed).
    // This is also the ONE gate every resource_* query above routes through,
    // so it is the one place `kind` is checked: a kind other than
    // RV_CD_RESOURCE_TEXTURE is refused with RV_ERR_INVAL right here, not in
    // each of the four callers. A second kind gets its own record type and
    // its own branch out of this gate, not a rewrite of the four callers.
    int64_t texture_resolve_(rv_cd_resource_kind kind, const char* resname, texture_record*& record_out);

    // Shared by texture_resolve_() and texture_reload(): open, measure, allocate
    // and read `resname`'s whole current contents into `bytes_out`. Returns
    // RV_OK or the negative rv_err either step answered.
    int64_t texture_read_bytes_(const char* resname, std::vector<std::byte>& bytes_out);

    // Stages of texture_resolve_(), split out to stay under the function-size
    // limit and so a mid-way failure has one clear place to free from.
    int64_t texture_decode_(const std::vector<std::byte>& bytes, rv_pdklib::rv_mppctex_header& header_out,
                             const std::byte*& palette_out, const std::byte*& texels_out) const;
    int64_t texture_upload_(const rv_pdklib::rv_mppctex_header& header, const std::byte* palette,
                             const std::byte* texels, int64_t& tex_addr_out, int64_t& pal_addr_out);
};

}  // namespace rv_3dmppc
