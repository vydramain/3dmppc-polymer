// The drive's DEVELOPMENT capability: refreshing a texture that is already
// resident. Its own translation unit because it is the only part of rv_pccd_fs
// that exists for the development runtime, and a player build links
// rv_pccd_fs_reload_null.cpp in its place (see the dev-capability slot in
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
    if (it == tex_by_name_.end()) return RV_ERR_NOENT;
    texture_record& record = textures_[static_cast<size_t>(it->second)];
    if (!record.live) return RV_ERR_NOENT;

    const int64_t handle = asset_open(resname);
    if (handle < 0) return handle;
    const int64_t size = asset_size(handle);
    if (size < 0) return size;

    std::vector<std::byte> bytes;
    try {
        bytes.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc&) {
        return RV_ERR_NOMEM;
    }
    const int64_t got = asset_read(handle, bytes.data(), size);
    if (got < 0) return got;
    // Same reason as in texture_acquire, and it bites harder here: a reload is
    // asked for precisely because the file changed, so a shrunk entry is the
    // expected case, not the exotic one.
    if (got != size) return RV_ERR_INVAL;

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
