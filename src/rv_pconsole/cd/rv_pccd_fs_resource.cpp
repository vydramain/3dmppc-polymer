// rv_pccd_fs resource accessor implementations: resolve addresses and metadata
// for textures and audio (resource_addr, resource_size, resource_palette_addr,
// resource_width, resource_height).
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

int64_t rv_pccd_fs::resource_addr(rv_cd_resource_kind kind, const char *resname)
{
    texture_record *texture = nullptr;
    audio_record *audio = nullptr;
    const int64_t rc = resource_resolve_(kind, resname, texture, audio);
    if (rc < 0) {
        return rc;
    }
    // Meaningful for both kinds (rv_cd.h): whichever record resolved is the
    // one this query answers.
    return texture != nullptr ? texture->tex_addr : audio->addr;
}

int64_t rv_pccd_fs::resource_size(rv_cd_resource_kind kind, const char *resname)
{
    texture_record *texture = nullptr;
    audio_record *audio = nullptr;
    const int64_t rc = resource_resolve_(kind, resname, texture, audio);
    if (rc < 0) {
        return rc;
    }
    // AUDIO-only (rv_cd.h): a texture resolved fine, but "size" is not one
    // of the things this contract lets a game read back about it - its shape
    // is width/height, below - so a resolved texture still refuses here
    // rather than making up a byte count nobody defined.
    if (audio != nullptr) {
        return audio->size;
    }
    return RV_ERR_INVAL;
}

int64_t rv_pccd_fs::resource_palette_addr(rv_cd_resource_kind kind, const char *resname)
{
    texture_record *texture = nullptr;
    audio_record *audio = nullptr;
    const int64_t rc = resource_resolve_(kind, resname, texture, audio);
    if (rc < 0) {
        return rc;
    }
    // TEXTURE-only (rv_cd.h): a sound has no palette to answer.
    if (texture != nullptr) {
        return texture->pal_addr;
    }
    return RV_ERR_INVAL;
}

int64_t rv_pccd_fs::resource_width(rv_cd_resource_kind kind, const char *resname)
{
    texture_record *texture = nullptr;
    audio_record *audio = nullptr;
    const int64_t rc = resource_resolve_(kind, resname, texture, audio);
    if (rc < 0) {
        return rc;
    }
    // TEXTURE-only (rv_cd.h): a sound has no pixel dimensions to answer.
    if (texture != nullptr) {
        return texture->width;
    }
    return RV_ERR_INVAL;
}

int64_t rv_pccd_fs::resource_height(rv_cd_resource_kind kind, const char *resname)
{
    texture_record *texture = nullptr;
    audio_record *audio = nullptr;
    const int64_t rc = resource_resolve_(kind, resname, texture, audio);
    if (rc < 0) {
        return rc;
    }
    // TEXTURE-only (rv_cd.h): a sound has no pixel dimensions to answer.
    if (texture != nullptr) {
        return texture->height;
    }
    return RV_ERR_INVAL;
}

} // namespace rv_3dmppc
