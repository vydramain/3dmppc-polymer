#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include <cstring>
#include <new>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc {

namespace {

uint16_t read_le_u16(const std::byte* p) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(p[0])) |
           (static_cast<uint16_t>(std::to_integer<uint8_t>(p[1])) << 8);
}

}  // namespace

rv_pcbudget_cost rv_pccd_fs::evaluate(const rv_pdklib::rv_manifest_budget& /*budget*/) {
    return {};
}

rv_pccd_fs::rv_pccd_fs(const rv_pccd_conf& conf)
    : conf_(conf), medium_(std::make_unique<rv_pcdirmedium>(conf.medium_path)) {}

const char* rv_pccd_fs::handle_name(int64_t handle) const {
    if (handle < 0 || handle >= static_cast<int64_t>(resnames_.size())) return nullptr;
    return resnames_[static_cast<size_t>(handle)].c_str();
}

rv_pccd_fs::texture_record* rv_pccd_fs::texture_record_of(int64_t res) {
    if (res <= 0) return nullptr;
    const int64_t index = res - 1;
    if (index >= static_cast<int64_t>(textures_.size())) return nullptr;
    texture_record& record = textures_[static_cast<size_t>(index)];
    if (!record.live) return nullptr;
    return &record;
}

int64_t rv_pccd_fs::asset_open(const char* resname) {
    // SECURITY: the name gate runs FIRST, before the table is consulted and long
    // before anything reaches the filesystem. A resource name addresses one entry
    // ON the inserted medium and has no syntax for leaving it, so `/`, `\` or
    // `..` is not a formatting slip - it is an attempt to read bytes that are not
    // on the disc. `../../etc/passwd` must never become an open() on the host, so
    // it is refused here rather than somewhere deeper where the medium might be
    // tempted to resolve it. See rv_pcresname_valid() in rv_pcmedium.hpp.
    if (!rv_pcresname_valid(resname)) {
        // TODO(rv_log_escape): 3 calls in this file. The console is its only
        // caller, so it does not belong in pdklib - find it a console-side home.
        //
        // The name goes through rv_pdklib::rv_log_escape() because it is exactly the string
        // an attack would put a newline or an ANSI escape into - see the note on
        // that function. Naming it matters: "an illegal name was rejected" tells
        // whoever reads the log nothing about WHICH asset the disc wanted.
        RV_LOG_WARN("pccd", "asset_open('{}') rejected: illegal resource name",
                    rv_pdklib::rv_log_escape(resname));
        return RV_ERR_INVAL;
    }

    // Handle table - resolution is idempotent. A name already in the
    // table returns its original handle, even if the entry has since vanished:
    // the contract promises a STABLE mapping from name to handle, and the fate of
    // the entry behind it is reported by asset_size / asset_read, not here.
    std::string key(resname);
    if (auto it = by_name_.find(key); it != by_name_.end()) return it->second;

    if (!medium_->mounted()) {
        // An empty drive is a legal machine, so this is "no such entry" and not a
        // device failure. Nothing is added to the table: inserting a disc later
        // must not find the name poisoned by a lookup made while the drive was
        // empty.
        // DBG, not WARN: an empty drive is a legal machine and the console
        // already said so once at boot. Repeating it as a warning on every
        // lookup is noise, and noise is what teaches people to stop reading
        // logs - the one thing a log cannot survive.
        RV_LOG_DBG("pccd", "asset_open('{}') with no medium mounted", rv_pdklib::rv_log_escape(key.c_str()));
        return RV_ERR_NOENT;
    }

    // Existence probe. The medium may answer RV_ERR_IO, which asset_open has no
    // way to express (its codes are INVAL / NOENT / NOMEM), so an entry that
    // cannot even be measured is reported as one that is not there - the honest
    // summary for a caller whose only question was "can this be resolved?".
    const int64_t size = medium_->entry_size(resname);
    if (size < 0) {
        RV_LOG_WARN("pccd", "asset_open('{}') found no readable entry (rc {})", key, size);
        return RV_ERR_NOENT;
    }

    if (static_cast<int64_t>(resnames_.size()) >= RV_PCCD_FS_RESOURCE_TABLE_MAX) {
        RV_LOG_ERR("pccd", "resource table full ({} entries); cannot resolve '{}'",
                   RV_PCCD_FS_RESOURCE_TABLE_MAX, key);
        return RV_ERR_NOMEM;
    }

    const int64_t handle = static_cast<int64_t>(resnames_.size());
    try {
        // The name is COPIED: the contract says `resname` need not outlive the
        // call, so the table may not hold the caller's pointer.
        resnames_.push_back(key);
        by_name_.emplace(std::move(key), handle);
    } catch (const std::bad_alloc&) {
        // Keep the two halves of the table consistent, then report the only code
        // the contract offers for "the table cannot grow".
        if (static_cast<int64_t>(resnames_.size()) > handle) resnames_.pop_back();
        RV_LOG_ERR("pccd", "out of memory growing the resource table");
        return RV_ERR_NOMEM;
    }

    RV_LOG_DBG("pccd", "resolved '{}' to handle {} ({} bytes)", resnames_.back(), handle, size);
    return handle;
}

// Size is a hint, not a promise - the drive reports the entry's size AT
// THE MOMENT OF THE CALL and nothing more. Between this call and the asset_read
// that follows, the medium is free to change underneath: a directory medium is
// live host filesystem, and a developer re-exporting an .obj mid-run genuinely
// does change the entry's length. Caching the size at asset_open() would make
// the drive assert a fact it cannot maintain, so it is re-measured every time and
// the byte count returned by asset_read() remains the only authoritative answer.
// This is why asset_read() refuses a short buffer rather than truncating: a game
// that sized its allocation from a now-stale hint must be told, not silently fed
// a fragment.
int64_t rv_pccd_fs::asset_size(int64_t handle) {
    const char* resname = handle_name(handle);
    if (resname == nullptr) {
        RV_LOG_WARN("pccd", "asset_size on unknown handle {}", handle);
        return RV_ERR_INVAL;
    }

    // The handle stays valid even with the drive empty; what it names does not.
    if (!medium_->mounted()) return RV_ERR_NOENT;

    return medium_->entry_size(resname);
}

int64_t rv_pccd_fs::asset_read(int64_t handle, void* baddr, int64_t baddr_size) {
    const char* resname = handle_name(handle);
    if (resname == nullptr) {
        RV_LOG_WARN("pccd", "asset_read on unknown handle {}", handle);
        return RV_ERR_INVAL;
    }
    // `baddr` must be non-null even for an empty entry (rv_cd.hpp): the drive
    // copies into memory the game owns and never allocates on its behalf.
    if (baddr == nullptr || baddr_size < 0) {
        RV_LOG_WARN("pccd", "asset_read('{}') with a malformed buffer (size {})", resname,
                    baddr_size);
        return RV_ERR_INVAL;
    }

    if (!medium_->mounted()) return RV_ERR_NOENT;

    // Whole-entry transfer, straight into the game's buffer. The medium checks
    // the capacity before it writes anything, so a short buffer costs the caller
    // an error code and not a clobbered allocation.
    return medium_->entry_read(resname, baddr, baddr_size);
}

// Reads `resname`'s whole current contents into `bytes_out`: open, measure,
// allocate, and read the full entry - the preparation texture_acquire() and
// texture_reload() both need before decoding. Medium re-measures the size on
// every call (see asset_size()'s comment above), so a short read here means
// the entry changed between the size call and the read; a reload is asked
// for precisely because the file changed, so that is where a shrunk entry is
// the expected case rather than the exotic one, and it is refused here
// rather than fed to a decoder as padding pretending to be pixels.
int64_t rv_pccd_fs::texture_read_bytes_(const char* resname, std::vector<std::byte>& bytes_out) {
    const int64_t handle = asset_open(resname);
    if (handle < 0) return handle;
    const int64_t size = asset_size(handle);
    if (size < 0) return size;

    try {
        bytes_out.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc&) {
        return RV_ERR_NOMEM;
    }
    const int64_t got = asset_read(handle, bytes_out.data(), size);
    if (got < 0) return got;
    // Medium re-measures each read; short count means entry shrank.
    // Padding bytes cannot go to video memory as false pixels.
    if (got != size) return RV_ERR_INVAL;

    return RV_OK;
}

// Parses the header this file's caller already read into `bytes`, and hands
// back pointers INTO `bytes` for the palette and texels - nothing is copied
// twice. Refuses anything mppcbaker would not have written: bad magic, a
// version this console does not speak, or fewer bytes than the header
// promises.
int64_t rv_pccd_fs::texture_decode_(const std::vector<std::byte>& bytes, rv_pdklib::rv_mppctex_header& header_out,
                                     const std::byte*& palette_out, const std::byte*& texels_out) const {
    if (static_cast<int64_t>(bytes.size()) < rv_pdklib::rv_mppctex_header_size) {
        return RV_ERR_INVAL;
    }

    const std::byte* raw = bytes.data();
    if (std::memcmp(raw + rv_pdklib::RV_MPPCTEX_OFF_MAGIC, rv_pdklib::rv_mppctex_magic,
                     sizeof(rv_pdklib::rv_mppctex_magic)) != 0) {
        return RV_ERR_INVAL;
    }

    const uint16_t version = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_VERSION);
    if (version != rv_pdklib::rv_mppctex_version) {
        return RV_ERR_INVAL;
    }

    rv_pdklib::rv_mppctex_header header;
    header.format = static_cast<rv_texfmt>(read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_FORMAT));
    header.width = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_WIDTH);
    header.height = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_HEIGHT);
    header.palette_count = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_PALETTE_COUNT);

    // The FORMAT decides whether a palette is required, and how big it may be.
    // Counting bytes alone is not enough: an IDX8 whose palette was deleted and
    // whose palette_count was zeroed has exactly the byte count its header
    // promises, and used to be accepted - the old texture was freed and the new
    // one sampled palette address 0. A paletted texture with no palette is not
    // a texture.
    switch (header.format) {
    case RV_TEXFMT_IDX4:
        if (header.palette_count == 0 || header.palette_count > 16) return RV_ERR_INVAL;
        break;
    case RV_TEXFMT_IDX8:
        if (header.palette_count == 0 || header.palette_count > 256) return RV_ERR_INVAL;
        break;
    case RV_TEXFMT_DIRECT15:
        // A direct texture samples no palette, so one here is a header
        // describing something this console cannot draw.
        if (header.palette_count != 0) return RV_ERR_INVAL;
        break;
    default:
        return RV_ERR_INVAL; // a format this console does not speak
    }
    // Zero of either dimension uploads nothing and draws nothing; it is a
    // corrupt header, not an empty picture.
    if (header.width == 0 || header.height == 0) {
        return RV_ERR_INVAL;
    }

    const int64_t palette_bytes = header.palette_count * rv_pdklib::rv_mppctex_palette_entry_bytes;
    const int64_t texel_bytes = rv_pdklib::rv_mppctex_texel_bytes(header);
    const int64_t need = rv_pdklib::rv_mppctex_header_size + palette_bytes + texel_bytes;
    if (static_cast<int64_t>(bytes.size()) < need) {
        return RV_ERR_INVAL;
    }

    header_out = header;
    palette_out = header.palette_count > 0 ? raw + rv_pdklib::rv_mppctex_header_size : nullptr;
    texels_out = raw + rv_pdklib::rv_mppctex_header_size + palette_bytes;
    return RV_OK;
}

// Allocates and uploads the palette (if any) and the texels. On any failure
// AFTER an allocation succeeded, that allocation is freed before returning:
// a failed acquire must leave video RAM exactly as it found it.
int64_t rv_pccd_fs::texture_upload_(const rv_pdklib::rv_mppctex_header& header, const std::byte* palette,
                                     const std::byte* texels, int64_t& tex_addr_out, int64_t& pal_addr_out) {
    int64_t pal_addr = 0;
    if (header.palette_count > 0) {
        const int64_t palette_bytes = header.palette_count * rv_pdklib::rv_mppctex_palette_entry_bytes;
        pal_addr = cv_->video_asset_malloc(palette_bytes);
        if (pal_addr < 0) return pal_addr;

        rv_texture pal_tex{};
        pal_tex.format = RV_TEXFMT_DIRECT15;
        pal_tex.data = palette;
        pal_tex.size = static_cast<uint64_t>(palette_bytes);
        pal_tex.width = static_cast<uint64_t>(header.palette_count);
        pal_tex.height = 1;
        const int64_t rc = cv_->video_asset_write(pal_addr, &pal_tex);
        if (rc < 0) {
            cv_->video_asset_free(pal_addr);
            return rc;
        }
    }

    const int64_t texel_bytes = rv_pdklib::rv_mppctex_texel_bytes(header);
    const int64_t tex_addr = cv_->video_asset_malloc(texel_bytes);
    if (tex_addr < 0) {
        if (pal_addr != 0) cv_->video_asset_free(pal_addr);
        return tex_addr;
    }

    rv_texture tex{};
    tex.format = header.format;
    tex.data = texels;
    tex.size = static_cast<uint64_t>(texel_bytes);
    tex.width = static_cast<uint64_t>(header.width);
    tex.height = static_cast<uint64_t>(header.height);
    const int64_t rc = cv_->video_asset_write(tex_addr, &tex);
    if (rc < 0) {
        cv_->video_asset_free(tex_addr);
        if (pal_addr != 0) cv_->video_asset_free(pal_addr);
        return rc;
    }

    tex_addr_out = tex_addr;
    pal_addr_out = pal_addr;
    return RV_OK;
}

int64_t rv_pccd_fs::texture_acquire(const char* resname) {
    // No video attached is the same situation as no medium mounted: a legal
    // machine state, not a caller error, so it answers the way asset_open
    // answers an unmounted drive - nothing can be made resident yet.
    if (cv_ == nullptr) return RV_ERR_INVAL;
    if (resname == nullptr) return RV_ERR_INVAL;

    std::string key(resname);
    if (auto it = tex_by_name_.find(key); it != tex_by_name_.end()) {
        texture_record& record = textures_[static_cast<size_t>(it->second)];
        if (record.live) {
            record.refs += 1;
            return it->second + 1;
        }
    }

    std::vector<std::byte> bytes;
    const int64_t read_rc = texture_read_bytes_(resname, bytes);
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

    texture_record record;
    record.resname = key;
    record.tex_addr = tex_addr;
    record.pal_addr = pal_addr;
    record.width = header.width;
    record.height = header.height;
    record.refs = 1;
    record.live = true;

    const int64_t index = static_cast<int64_t>(textures_.size());
    try {
        textures_.push_back(std::move(record));
        tex_by_name_[key] = index;
    } catch (const std::bad_alloc&) {
        cv_->video_asset_free(tex_addr);
        if (pal_addr != 0) cv_->video_asset_free(pal_addr);
        if (static_cast<int64_t>(textures_.size()) > index) textures_.pop_back();
        return RV_ERR_NOMEM;
    }

    return index + 1;
}

int64_t rv_pccd_fs::texture_release(int64_t res) {
    if (cv_ == nullptr) return RV_ERR_INVAL;

    texture_record* record = texture_record_of(res);
    if (record == nullptr) return RV_ERR_INVAL;

    record->refs -= 1;
    if (record->refs > 0) return RV_OK;

    cv_->video_asset_free(record->tex_addr);
    if (record->pal_addr != 0) cv_->video_asset_free(record->pal_addr);

    // The slot itself is never reused (see the member comment on textures_):
    // only the name lookup is undone, so a later acquire of this name starts
    // fresh instead of colliding with a dead record.
    tex_by_name_.erase(record->resname);
    record->live = false;
    return RV_OK;
}

int64_t rv_pccd_fs::texture_addr(int64_t res) {
    const texture_record* record = texture_record_of(res);
    if (record == nullptr) return RV_ERR_INVAL;
    return record->tex_addr;
}

int64_t rv_pccd_fs::texture_palette_addr(int64_t res) {
    const texture_record* record = texture_record_of(res);
    if (record == nullptr) return RV_ERR_INVAL;
    return record->pal_addr;
}

int64_t rv_pccd_fs::texture_width(int64_t res) {
    const texture_record* record = texture_record_of(res);
    if (record == nullptr) return RV_ERR_INVAL;
    return record->width;
}

int64_t rv_pccd_fs::texture_height(int64_t res) {
    const texture_record* record = texture_record_of(res);
    if (record == nullptr) return RV_ERR_INVAL;
    return record->height;
}

}  // namespace rv_3dmppc
