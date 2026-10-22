// The drive's DEVELOPMENT capability: refreshing an asset that is already
// resident, by the kind it is resident as. Its own translation unit because it is the only part of rv_pccd_fs
// that exists for the development runtime, and a player build links
// rv_pccd_fs_reload_standard.cpp in its place (see the dev-capability slot in
// CMakeLists.txt) - so the decoder, the upload and the swap are not in that
// binary at all.
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_textures/rv_mppctex.hpp"

namespace rv_3dmppc
{

// One branch per kind a name can be resident as; a new kind adds its branch
// here, next to its own residency table.
int64_t rv_pccd_fs::asset_reload(const char *resname, rv_cd_resource_kind &kind_out)
{
    if (resname == nullptr) {
        return RV_ERR_INVAL;
    }

    const std::string key(resname);
    if (auto it = tex_by_name_.find(key); it != tex_by_name_.end()) {
        kind_out = RV_CD_RESOURCE_TEXTURE;
        return texture_reload_(resname, it->second);
    }
    if (audio_by_name_.find(key) != audio_by_name_.end()) {
        // A resident sound is not "nothing to refresh": the bytes did change,
        // the drive just cannot follow them under a voice that is already
        // reading the old block.
        kind_out = RV_CD_RESOURCE_AUDIO;
        return RV_PCCD_UNSUPPORTED_KIND;
    }
    if (std::find(scene_names_.begin(), scene_names_.end(), key) != scene_names_.end()) {
        return RV_PCCD_SCENE_READ_AT_START;
    }
    return RV_PCCD_NOT_RESIDENT;
}

// Re-reads `resname` off the medium and hands the bytes to texture_refresh_.
int64_t rv_pccd_fs::texture_reload_(const char *resname, int64_t index)
{
    std::vector<std::byte> bytes;
    const int64_t read_rc = asset_read_bytes_(resname, bytes);
    if (read_rc < 0) {
        return read_rc;
    }

    return texture_refresh_(index, bytes);
}

// Decodes and re-uploads `bytes` and prepares a whole NEW upload before
// touching the live record at all: the old blocks are only freed once the
// new ones are fully written, so a failed refresh leaves the old texture
// exactly as it was rather than a torn or freed-then-hoped-for one. Shared by
// texture_reload_ (medium bytes) and asset_refresh (request bytes).
int64_t rv_pccd_fs::texture_refresh_(int64_t index, const std::vector<std::byte> &bytes)
{
    if (cv_ == nullptr) {
        return RV_ERR_INVAL;
    }
    texture_record &record = textures_[static_cast<size_t>(index)];

    rv_pdklib::rv_mppctex_header header;
    const std::byte *palette = nullptr;
    const std::byte *texels = nullptr;
    const int64_t decode_rc = texture_decode_(bytes, header, palette, texels);
    if (decode_rc < 0) {
        return decode_rc;
    }

    int64_t tex_addr = 0;
    int64_t pal_addr = 0;
    const int64_t upload_rc = texture_upload_(header, palette, texels, tex_addr, pal_addr);
    if (upload_rc < 0) {
        return upload_rc;
    }

    // Models move to the new blocks before the old ones are freed. A model that
    // cannot go back keeps the new blocks, so they stay too.
    const rv_pccg_part_texture from{ record.tex_addr, record.pal_addr, record.width, record.height };
    const rv_pccg_part_texture to{ tex_addr, pal_addr, header.width, header.height };
    bool stuck = false;
    const int64_t models_rc = models_retexture_(index, from, to, stuck);
    const bool release_new = models_rc < 0 && !stuck;
    if (release_new) {
        video_asset_release_(tex_addr);
        video_asset_release_(pal_addr);
    }
    if (models_rc < 0) {
        return models_rc;
    }

    // Everything new is up and written; only now is it safe to drop the old.
    const int64_t old_tex_addr = record.tex_addr;
    const int64_t old_pal_addr = record.pal_addr;
    record.tex_addr = tex_addr;
    record.pal_addr = pal_addr;
    record.width = header.width;
    record.height = header.height;
    video_asset_release_(old_tex_addr);
    video_asset_release_(old_pal_addr);

    return RV_OK;
}

// asset_reload's twin for bytes the request carried: same per-kind dispatch,
// refreshing from `bytes` instead of re-reading the medium.
int64_t rv_pccd_fs::asset_refresh(const char *resname, const void *bytes, int64_t nbytes, rv_cd_resource_kind &kind_out)
{
    if (resname == nullptr || bytes == nullptr || nbytes < 0) {
        return RV_ERR_INVAL;
    }

    const std::string key(resname);
    if (auto it = tex_by_name_.find(key); it != tex_by_name_.end()) {
        kind_out = RV_CD_RESOURCE_TEXTURE;
        const auto *first = static_cast<const std::byte *>(bytes);
        const std::vector<std::byte> payload(first, first + nbytes);
        return texture_refresh_(it->second, payload);
    }
    if (audio_by_name_.find(key) != audio_by_name_.end()) {
        kind_out = RV_CD_RESOURCE_AUDIO;
        return RV_PCCD_UNSUPPORTED_KIND;
    }
    if (std::find(scene_names_.begin(), scene_names_.end(), key) != scene_names_.end()) {
        return RV_PCCD_SCENE_READ_AT_START;
    }
    return RV_PCCD_NOT_RESIDENT;
}

} // namespace rv_3dmppc
