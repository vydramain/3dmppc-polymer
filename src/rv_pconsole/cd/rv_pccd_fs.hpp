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
#include <unordered_set>
#include <utility>
#include <vector>

#include "pdklib/rv_textures/rv_mppctex.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cd/rv_pccd_stream.hpp"
#include "rv_pconsole/cd/rv_pcmedium.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{
class rv_pccd_fs final : public rv_pccd
{
private:
    rv_pccd_conf conf_;

    // What is inserted. Held by pointer purely so the medium KIND can change
    // without this class changing (strategy); it is never null, an
    // empty drive is a mounted-less medium rather than a missing one.
    std::unique_ptr<rv_pcmedium> medium_;

    // BORROWED, set by video_attach() after construction (rv_pccd.hpp: cd_ is
    // built before cv_ exists, so this cannot be a constructor reference).
    // Null until attached, which the five rv_cd_resource_* functions (addr,
    // size, palette_addr, width, height) treat the same way asset_open treats
    // an unmounted medium: not an error, just nothing resident yet.
    rv_pccv *cv_ = nullptr;

    // audio_attach()'s twin of cv_ above: same borrow, same reason, same
    // "nothing resident yet" meaning while null.
    rv_pcca *ca_ = nullptr;

    // geometry_attach()'s twin of cv_ above: same borrow, same "nothing
    // resident yet" meaning while null; why it is a borrow - see geometry_attach()
    // in rv_pccd.hpp.
    rv_pccg *cg_ = nullptr;

    // The one streamed entry: owns the staging pump() fills; the ring is ca_'s.
    rv_pccd_stream stream_;

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

    // AUDIO's counterpart of texture_record above: raw PCM bytes made
    // resident in sound RAM, verbatim - there is no container to decode, so
    // there is no width/height/palette to keep, only where the bytes live
    // and how many of them there are. Same growth/freeing rules as
    // `textures_`.
    struct audio_record {
        std::string resname;
        int64_t addr = 0;
        int64_t size = 0;
    };
    std::vector<audio_record> audios_;
    std::unordered_map<std::string, int64_t> audio_by_name_;
    // Names whose resident_audio_() refusal is already in the log: one line per name
    // for the drive's life, since the disc asks again on every play. The code is
    // returned on every attempt. Below it, asset_open logs a failed size measure and a
    // failed table growth, and asset_read logs a medium read that fails with RV_ERR_IO,
    // each on every attempt. Silent: NOENT, a full table, a read buffer out of memory,
    // and every other read code.
    std::unordered_set<std::string> audio_refused_logged_;

    // MODEL's counterpart of texture_record: one model uploaded to the geometry
    // controller, which owns its memory and hands back `addr`. Same growth rules
    // as `textures_`; freed by free_models_() before the textures it refers to.
    struct model_record {
        std::string resname;
        int64_t addr = 0;
        // One entry per part, as the controller holds it, and per part the index of
        // its texture in textures_ (-1 = none). A refresh matches parts by that index,
        // never by address: a null video controller can hand out one address for all.
        std::vector<rv_pccg_part_texture> textures;
        std::vector<int64_t> part_texture;
    };
    std::vector<model_record> models_;
    std::unordered_map<std::string, int64_t> model_by_name_;

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

    // Names of every asset this drive has opened, each name once, in open
    // order. Cleared on medium_insert(), which runs once at boot. Not part of
    // the rv_cd contract - a disc never asks a drive what it opened - this is
    // read by the devtools command layer only, to tell the editor which assets
    // the running disc used.
    std::vector<std::string> asset_names_;

    // Bumped in medium_insert(), after asset_names_ is cleared. The devtools
    // side keeps the generation it last saw next to its own send cursor;
    // a mismatch means asset_names_ started over, so the cursor must too,
    // even when the new list is as long or longer than the old one.
    uint64_t asset_generation_ = 0;

public:
    explicit rv_pccd_fs(const rv_pccd_conf &conf);
    // The drive's teardown: a disc never releases a texture, so this is the
    // only place any of them are freed - everything the drive ever made
    // resident for this disc goes back to video RAM here, all at once.
    ~rv_pccd_fs() override;

    rv_pccd_fs(const rv_pccd_fs &) = delete;
    rv_pccd_fs &operator=(const rv_pccd_fs &) = delete;

    // Counts the stream staging (RV_PCCD_STREAM_PORTION_FRAMES mono S16 frames, 2 bytes
    // each); assets are read from the disc's own archive, not from a budgeted pool.
    static int evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

    int64_t asset_open(const char *resname) override;

    // Read-only view of asset_names_ above.
    const std::vector<std::string> &asset_names() const
    {
        return asset_names_;
    }

    // Read-only view of asset_generation_ above.
    uint64_t asset_generation() const
    {
        return asset_generation_;
    }

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void *baddr, int64_t baddr_size) override;

    int64_t resource_addr(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_size(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_palette_addr(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_width(rv_cd_resource_kind kind, const char *resname) override;

    int64_t resource_height(rv_cd_resource_kind kind, const char *resname) override;

    int64_t asset_reload(const char *resname, rv_cd_resource_kind &kind_out) override;

    // asset_reload's twin for bytes the request carried: same result codes,
    // refreshing whatever kind `resname` is resident as from `bytes` instead
    // of the medium.
    int64_t asset_refresh(const char *resname, const void *bytes, int64_t nbytes, rv_cd_resource_kind &kind_out) override;

    // Streamed music, forwarded to stream_ with the medium and the ring.
    int64_t stream_play(const char *resname, const rv_cd_stream_conf *conf) override;

    int64_t stream_stop() override;

    int64_t stream_status(rv_cd_stream_info *out) override;

    int64_t stream_queue(const char *resname, const rv_cd_stream_conf *conf) override;

    int64_t stream_switch(const char *resname, const rv_cd_stream_conf *conf) override;

    int64_t stream_queue_status(rv_cd_stream_queue_info *out) override;

    int64_t stream_pump() override;

    // Swap the inserted medium after construction. The console learns
    // WHICH archive to mount only when it has loaded the disc out of it, which
    // is later than this object is built; the conf-built directory medium (the
    // catalogue path) is untouched and stays the default. Strategy -
    // this is the one seam where the strategy is chosen, and it is deliberately
    // the only one.
    void medium_insert(std::unique_ptr<rv_pcmedium> medium) override
    {
        if (!medium) {
            return;
        }
        medium_ = std::move(medium);
        // A new medium has opened nothing yet: last medium's assets do not
        // carry over.
        asset_names_.clear();
        ++asset_generation_;
    }

    void video_attach(rv_pccv &cv) override
    {
        cv_ = &cv;
    }

    void audio_attach(rv_pcca &ca) override
    {
        ca_ = &ca;
    }

    // Called once, before any model is resident: the drive keeps this pointer
    // and frees its models through it at teardown, so a second controller
    // would leave the first one's models unfreed.
    void geometry_attach(rv_pccg &cg) override
    {
        cg_ = &cg;
    }

    // An empty or unmountable medium leaves the drive empty, never broken.
    bool valid() const override
    {
        return true;
    }

private:
    // The upper bound on distinct names one disc may resolve. It exists so that
    // RV_ERR_NOMEM is a real, testable answer ("the resource table cannot grow")
    // instead of a code that only ever appears when the host is already dying.
    static constexpr int64_t RV_PCCD_FS_RESOURCE_TABLE_MAX = 4096;

    // Name behind a handle, or nullptr when the handle was never issued.
    const char *handle_name(int64_t handle) const;

    // The ONE gate every resource_* query above routes through, so it is the
    // one place `kind` is checked: a kind other than RV_CD_RESOURCE_TEXTURE,
    // RV_CD_RESOURCE_AUDIO or RV_CD_RESOURCE_MODEL is refused with RV_ERR_INVAL
    // right here, not in each of the five callers. Exactly one of `texture_out`,
    // `audio_out` and `model_out` is set on success, matching the kind that
    // actually resolved; the others are left null. A caller whose query does not
    // describe that kind (say, resource_palette_addr against an AUDIO name) reads
    // the null it got back and answers RV_ERR_INVAL itself - it never re-derives
    // the kind check. A fourth kind adds its own out-pointer and branch out of
    // this gate; the callers declare that pointer and pass it through.
    // On failure all output pointers are left null and the return is the
    // negative rv_err reading, decoding or uploading answered (the same
    // rv_err asset_open would give the name, when that is where it failed).
    int64_t resource_resolve_(rv_cd_resource_kind kind,
        const char *resname,
        texture_record *&texture_out,
        audio_record *&audio_out,
        model_record *&model_out);

    // Cache-hit half of resource_resolve_() for TEXTURE: a resident name answers from
    // textures_, a new one goes to texture_resolve_().
    int64_t resident_texture_(const char *resname, texture_record *&texture_out);

    // The same for AUDIO: a resident name answers from audios_, a new one goes to
    // audio_resolve_().
    // Defined in rv_pccd_fs_audio.cpp.
    int64_t resident_audio_(const char *resname, audio_record *&audio_out);

    // The same for MODEL: a resident name answers from models_, a new one goes to
    // model_resolve_(). Refused while no geometry controller is attached.
    int64_t resident_model_(const char *resname, model_record *&model_out);

    // Cache miss half of resource_resolve_() for TEXTURE: read, decode and
    // upload `resname`, then remember it in `textures_`. Same
    // success/failure contract as resource_resolve_() itself.
    int64_t texture_resolve_(const char *resname, texture_record *&record_out);

    // Cache miss half of resource_resolve_() for AUDIO: read `resname`'s raw
    // bytes - there is nothing to decode, the on-disc format IS the resident
    // format - and upload them into sound RAM through the borrowed ca_, then
    // remember it in `audios_`. Same success/failure contract as
    // resource_resolve_() itself.
    // Defined in rv_pccd_fs_audio.cpp.
    int64_t audio_resolve_(const char *resname, audio_record *&record_out);

    // Cache miss half of resource_resolve_() for MODEL: parse `resname`'s
    // .mppcmod, make each textured part's texture resident through
    // resident_texture_(), upload the model through cg_, then remember it in
    // `models_`. Same success/failure contract as resource_resolve_() itself.
    int64_t model_resolve_(const char *resname, model_record *&record_out);

    // Frees every model this drive uploaded, through cg_. Called from
    // ~rv_pccd_fs() before the textures are freed: a model holds their addresses.
    void free_models_();

    // Moves every model whose parts read texture `index` from `from` to `to` (same
    // model address, new entries). All or nothing: when the controller refuses one
    // model, the models already moved go back. `stuck` is set when a model could not
    // go back, so it still reads the new blocks and the caller must keep them.
    // Defined in rv_pccd_fs_model.cpp.
    int64_t models_retexture_(int64_t index, const rv_pccg_part_texture &from, const rv_pccg_part_texture &to, bool &stuck);

    // models_retexture_()'s rollback over the first `moved` models back to `from`;
    // returns the first failure, RV_OK when all went back.
    int64_t models_revert_(int64_t index, const rv_pccg_part_texture &from, size_t moved);

    // One model's step: sets the parts that read `index` to `next` and asks the
    // controller; a refusal restores the model's entries as they were.
    int64_t model_retexture_(model_record &model, int64_t index, const rv_pccg_part_texture &next);

    // Shared by texture_resolve_(), audio_resolve_(), model_resolve_() and
    // texture_reload_(): open, measure, allocate and read `resname`'s whole current
    // contents into `bytes_out`. Returns RV_OK or the negative rv_err either step
    // answered. Named for what it reads (an asset's bytes), not for who
    // reads them, because both resource kinds and the dev reload path share
    // it verbatim.
    int64_t asset_read_bytes_(const char *resname, std::vector<std::byte> &bytes_out);

    // Stages of texture_resolve_(), split out to stay under the function-size
    // limit and so a mid-way failure has one clear place to free from.
    int64_t texture_decode_(const std::vector<std::byte> &bytes,
        rv_pdklib::rv_mppctex_header &header_out,
        const std::byte *&palette_out,
        const std::byte *&texels_out) const;
    int64_t texture_upload_(const rv_pdklib::rv_mppctex_header &header,
        const std::byte *palette,
        const std::byte *texels,
        int64_t &tex_addr_out,
        int64_t &pal_addr_out);

    // Frees a video block; 0 means none. Needs cv_ attached.
    void video_asset_release_(int64_t addr);

    // asset_reload()'s TEXTURE branch: refreshes `record` from `resname`'s
    // current bytes on the medium. Defined with asset_reload(), in the
    // development half.
    int64_t texture_reload_(const char *resname, int64_t index);

    // Shared decode/upload/swap behind both texture_reload_() (bytes read
    // from the medium) and asset_refresh() (bytes the request carried): the
    // one place a texture record is actually refreshed from a byte buffer.
    // Defined with asset_reload(), in the development half.
    int64_t texture_refresh_(int64_t index, const std::vector<std::byte> &bytes);
};

} // namespace rv_3dmppc
