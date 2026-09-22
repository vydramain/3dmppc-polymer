// The drive's DEVELOPMENT capability: refreshing a texture that is already
// resident. Its own translation unit because it is the only part of rv_pccd_fs
// that exists for the development runtime, and a player build links
// rv_pccd_fs_reload_standard.cpp in its place (see the dev-capability slot in
// CMakeLists.txt) - so the decoder, the upload and the swap are not in that
// binary at all.
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include <cstddef>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_textures/rv_mppctex.hpp"

namespace rv_3dmppc {

// Re-reads and re-decodes `resname` and prepares a whole NEW upload before
// touching the live record at all: the old blocks are only freed once the
// new ones are fully written, so a failed reload leaves the old texture
// exactly as it was rather than a torn or freed-then-hoped-for one.
int64_t rv_pccd_fs::texture_reload(const char* resname) {
    if (cv_ == nullptr) return RV_ERR_INVAL;
    if (resname == nullptr) return RV_ERR_INVAL;

    std::string key(resname);
    auto it = tex_by_name_.find(key);
    if (it == tex_by_name_.end()) {
        // A sound the disc made resident is not "nothing to refresh": the
        // bytes did change, the drive just cannot follow them under a voice
        // that is already reading the old block.
        if (audio_by_name_.find(key) != audio_by_name_.end()) return RV_PCCD_WRONG_KIND;
        return RV_PCCD_NOT_RESIDENT;
    }
    texture_record& record = textures_[static_cast<size_t>(it->second)];

    std::vector<std::byte> bytes;
    const int64_t read_rc = asset_read_bytes_(resname, bytes);
    if (read_rc < 0) return read_rc;

    rv_pdklib::rv_mppctex_header header;
    const std::byte* palette = nullptr;
    const std::byte* texels = nullptr;
    const int64_t decode_rc = texture_decode_(bytes, header, palette, texels);
    if (decode_rc < 0) return decode_rc;

    int64_t tex_addr = 0;
    int64_t pal_addr = 0;
    const int64_t upload_rc = texture_upload_(header, palette, texels, tex_addr, pal_addr);
    if (upload_rc < 0) return upload_rc;

    // Everything new is up and written; only now is it safe to drop the old.
    const int64_t old_tex_addr = record.tex_addr;
    const int64_t old_pal_addr = record.pal_addr;
    record.tex_addr = tex_addr;
    record.pal_addr = pal_addr;
    record.width = header.width;
    record.height = header.height;
    cv_->video_asset_free(old_tex_addr);
    if (old_pal_addr != 0) cv_->video_asset_free(old_pal_addr);

    return RV_OK;
}

}  // namespace rv_3dmppc
