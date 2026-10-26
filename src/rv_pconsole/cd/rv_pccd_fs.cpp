#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include <new>
#include <string>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace
{
// Upload one resource to video memory: allocate, write, free on error.
// Returns address (>= 0) or error code (< 0).
int64_t video_asset_upload_(rv_3dmppc::rv_pccv &cv, int64_t size, const rv_texture &tex)
{
    const int64_t addr = cv.video_asset_malloc(size);
    if (addr < 0) {
        return addr;
    }
    const int64_t rc = cv.video_asset_write(addr, &tex);
    if (rc < 0) {
        cv.video_asset_free(addr);
        return rc;
    }
    return addr;
}

// Upload palette if present, or return 0 if no palette in format.
// Returns address (> 0), 0 if no palette, or error code (< 0).
int64_t palette_upload_(rv_3dmppc::rv_pccv &cv, const rv_pdklib::rv_mppctex_header &header, const std::byte *palette)
{
    if (header.palette_count <= 0) {
        return 0;
    }
    const int64_t palette_bytes = header.palette_count * rv_pdklib::rv_mppctex_palette_entry_bytes;
    rv_texture pal_tex{};
    pal_tex.format = RV_TEXFMT_DIRECT15;
    pal_tex.data = palette;
    pal_tex.size = static_cast<uint64_t>(palette_bytes);
    pal_tex.width = static_cast<uint64_t>(header.palette_count);
    pal_tex.height = 1;
    return video_asset_upload_(cv, palette_bytes, pal_tex);
}
} // anonymous namespace

namespace rv_3dmppc
{

int rv_pccd_fs::evaluate(const rv_pdklib::rv_manifest_budget & /*budget*/, int64_t &bytes, std::string & /*error*/)
{
    // The rv_pccd_stream staging buffer is allocated once, in the constructor.
    bytes = static_cast<int64_t>(RV_PCCD_STREAM_PORTION_FRAMES * sizeof(int16_t));
    return RV_OK;
}

rv_pccd_fs::rv_pccd_fs(const rv_pccd_conf &conf)
    : conf_(conf)
    , medium_(std::make_unique<rv_pcdirmedium>(conf.medium_path))
{
}

rv_pccd_fs::~rv_pccd_fs()
{
    // Stream first, while ca_ is alive: pump is not called once the drive is destroyed, so clear its input here.
    if (ca_ != nullptr) {
        static_cast<void>(stream_.stop(*ca_));
    }
    // A disc never releases a texture, a sound or a model; this is where the
    // drive frees every one it made resident, all at once, when the disc that
    // named them goes away. Models go first: their parts point at the textures
    // freed below. cv_/ca_ are null only when video/audio was never
    // attached, which means nothing of that kind was ever uploaded and there
    // is nothing to free.
    free_models_();
    if (cv_ != nullptr) {
        for (const texture_record &record : textures_) {
            cv_->video_asset_free(record.tex_addr);
            if (record.pal_addr != 0) {
                cv_->video_asset_free(record.pal_addr);
            }
        }
    }
    if (ca_ != nullptr) {
        for (const audio_record &record : audios_) {
            ca_->sound_asset_free(record.addr);
        }
    }
}

const char *rv_pccd_fs::handle_name(int64_t handle) const
{
    if (handle < 0 || handle >= static_cast<int64_t>(resnames_.size())) {
        return nullptr;
    }
    return resnames_[static_cast<size_t>(handle)].c_str();
}

int64_t rv_pccd_fs::asset_open(const char *resname)
{
    // SECURITY: the name gate runs FIRST, before the table is consulted and long
    // before anything reaches the filesystem. A resource name addresses one entry
    // ON the inserted medium and has no syntax for leaving it, so `/`, `\` or
    // `..` is not a formatting slip - it is an attempt to read bytes that are not
    // on the disc. `../../etc/passwd` must never become an open() on the host, so
    // it is refused here rather than somewhere deeper where the medium might be
    // tempted to resolve it. See rv_pcresname_valid() in rv_pcmedium.hpp.
    if (!rv_pcresname_valid(resname)) {
        return RV_ERR_INVAL;
    }

    // Handle table - resolution is idempotent. A name already in the
    // table returns its original handle, even if the entry has since vanished:
    // the contract promises a STABLE mapping from name to handle, and the fate of
    // the entry behind it is reported by asset_size / asset_read, not here.
    std::string key(resname);
    if (auto it = by_name_.find(key); it != by_name_.end()) {
        return it->second;
    }

    if (!medium_->mounted()) {
        // An empty drive is a legal machine, so this is "no such entry" and not a
        // device failure. Nothing is added to the table: inserting a disc later
        // must not find the name poisoned by a lookup made while the drive was
        // empty.
        // DBG, not WARN: an empty drive is a legal machine and the console
        // already said so once at boot. Repeating it as a warning on every
        // lookup is noise, and noise is what teaches people to stop reading
        // logs - the one thing a log cannot survive.
        // TODO(rv_log_escape): 3 calls in this file. The console is its only
        // caller, so it does not belong in pdklib - find it a console-side home.
        RV_LOG_DBG("pccd", "asset_open('{}') with no medium mounted", rv_pdklib::rv_log_escape(key.c_str()));
        return RV_ERR_NOENT;
    }

    // Existence probe. NOENT from the medium is the disc's answer and stays silent.
    // Any other failure is the console's own: asset_open cannot express RV_ERR_IO
    // (its codes are INVAL / NOENT / NOMEM), so it is logged here and the disc gets
    // NOENT, the honest summary for a caller whose only question was "can this be resolved?".
    const int64_t size = medium_->entry_size(resname);
    if (size == RV_ERR_NOENT) {
        return RV_ERR_NOENT;
    }
    if (size < 0) {
        RV_LOG_ERR("pccd",
            "asset_open('{}') cannot measure the entry (rv_err {})",
            rv_pdklib::rv_log_escape(key.c_str()),
            size);
        return RV_ERR_NOENT;
    }

    if (static_cast<int64_t>(resnames_.size()) >= RV_PCCD_FS_RESOURCE_TABLE_MAX) {
        return RV_ERR_NOMEM;
    }

    const int64_t handle = static_cast<int64_t>(resnames_.size());
    try {
        // The name is COPIED: the contract says `resname` need not outlive the
        // call, so the table may not hold the caller's pointer.
        resnames_.push_back(key);
        by_name_.emplace(key, handle);
        // Recorded once per name for this drive: by_name_ above returns early on a
        // repeat, so this runs only the first time the name resolves.
        asset_names_.push_back(key);
    } catch (const std::bad_alloc &) {
        // Undo every table this call may have grown, so the three stay consistent,
        // then report the only code the contract offers for "the table cannot grow".
        if (static_cast<int64_t>(resnames_.size()) > handle) {
            resnames_.pop_back();
        }
        by_name_.erase(key);
        if (!asset_names_.empty() && asset_names_.back() == key) {
            asset_names_.pop_back();
        }
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
int64_t rv_pccd_fs::asset_size(int64_t handle)
{
    const char *resname = handle_name(handle);
    if (resname == nullptr) {
        return RV_ERR_INVAL;
    }

    // The handle stays valid even with the drive empty; what it names does not.
    if (!medium_->mounted()) {
        return RV_ERR_NOENT;
    }

    return medium_->entry_size(resname);
}

int64_t rv_pccd_fs::asset_read(int64_t handle, void *baddr, int64_t baddr_size)
{
    const char *resname = handle_name(handle);
    if (resname == nullptr) {
        return RV_ERR_INVAL;
    }
    // `baddr` must be non-null even for an empty entry (rv_cd.hpp): the drive
    // copies into memory the game owns and never allocates on its behalf.
    if (baddr == nullptr || baddr_size < 0) {
        return RV_ERR_INVAL;
    }

    if (!medium_->mounted()) {
        return RV_ERR_NOENT;
    }

    // Whole-entry transfer, straight into the game's buffer. The medium checks
    // the capacity before it writes anything, so a short buffer costs the caller
    // an error code and not a clobbered allocation.
    const int64_t got = medium_->entry_read(resname, baddr, baddr_size);
    // RV_ERR_IO is the console's own failure (the device, not the disc's request);
    // the disc still gets the code as is.
    if (got == RV_ERR_IO) {
        RV_LOG_ERR("pccd", "asset_read('{}') failed in the medium (rv_err {})", rv_pdklib::rv_log_escape(resname), got);
    }
    return got;
}

// Reads `resname`'s whole current contents into `bytes_out`: open, measure,
// allocate, and read the full entry - the preparation texture_resolve_(),
// audio_resolve_() and texture_reload_() all need before doing anything kind-
// specific with the bytes (decoding, for a texture; nothing at all, for raw
// PCM audio). Medium re-measures the size on every call (see asset_size()'s
// comment above), so a short read here means the entry changed between the
// size call and the read; a reload is asked for precisely because the file
// changed, so that is where a shrunk entry is the expected case rather than
// the exotic one, and it is refused here rather than fed to a texture
// decoder as padding pretending to be pixels, or played back to the SPU as
// padding pretending to be samples.
int64_t rv_pccd_fs::asset_read_bytes_(const char *resname, std::vector<std::byte> &bytes_out)
{
    const int64_t handle = asset_open(resname);
    if (handle < 0) {
        return handle;
    }
    const int64_t size = asset_size(handle);
    if (size < 0) {
        return size;
    }

    try {
        bytes_out.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc &) {
        return RV_ERR_NOMEM;
    }
    const int64_t got = asset_read(handle, bytes_out.data(), size);
    if (got < 0) {
        return got;
    }
    // Medium re-measures each read; short count means entry shrank.
    // Padding bytes cannot go to video memory as false pixels.
    if (got != size) {
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

// Parses the header this file's caller already read into `bytes`, and hands
// back pointers INTO `bytes` for the palette and texels - nothing is copied
// twice. Every rule this console enforces on a .mppctex - magic, version,
// format, palette shape, non-zero dimensions, enough bytes for the payload -
// lives in rv_pdklib::rv_mppctex_parse() now, because mppcburner and a
// hot-reload must refuse exactly the same broken file this console would,
// not a looser subset of it. Only the translation to this contract's rv_err
// stays here: `error` is the human sentence a CLI would print, and a disc's
// asset_open() has nowhere to print it, so it is discarded.
int64_t rv_pccd_fs::texture_decode_(const std::vector<std::byte> &bytes,
    rv_pdklib::rv_mppctex_header &header_out,
    const std::byte *&palette_out,
    const std::byte *&texels_out) const
{
    std::string error;
    if (rv_pdklib::rv_mppctex_parse(bytes, header_out, palette_out, texels_out, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Frees a video block; 0 means no block (a format without a palette).
void rv_pccd_fs::video_asset_release_(int64_t addr)
{
    if (addr == 0) {
        return;
    }
    cv_->video_asset_free(addr);
}

// Allocates and uploads the palette (if any) and the texels. On any failure
// AFTER an allocation succeeded, that allocation is freed before returning:
// a failed acquire must leave video RAM exactly as it found it.
int64_t rv_pccd_fs::texture_upload_(const rv_pdklib::rv_mppctex_header &header,
    const std::byte *palette,
    const std::byte *texels,
    int64_t &tex_addr_out,
    int64_t &pal_addr_out)
{
    const int64_t pal_addr = palette_upload_(*cv_, header, palette);
    if (pal_addr < 0) {
        return pal_addr;
    }

    const int64_t texel_bytes = rv_pdklib::rv_mppctex_texel_bytes(header);
    rv_texture tex{};
    tex.format = header.format;
    tex.data = texels;
    tex.size = static_cast<uint64_t>(texel_bytes);
    tex.width = static_cast<uint64_t>(header.width);
    tex.height = static_cast<uint64_t>(header.height);
    const int64_t tex_addr = video_asset_upload_(*cv_, texel_bytes, tex);
    if (tex_addr < 0) {
        video_asset_release_(pal_addr);
        return tex_addr;
    }

    tex_addr_out = tex_addr;
    pal_addr_out = pal_addr;
    return RV_OK;
}

// The one gate every resource_* query routes through - see this function's
// own comment in rv_pccd_fs.hpp for the shape (exactly one of the two output
// pointers set on success, the other left null) and why it stays one gate
// rather than five copies of the same kind check.
int64_t rv_pccd_fs::resource_resolve_(rv_cd_resource_kind kind,
    const char *resname,
    texture_record *&texture_out,
    audio_record *&audio_out,
    model_record *&model_out)
{
    texture_out = nullptr;
    audio_out = nullptr;
    model_out = nullptr;
    if (resname == nullptr) {
        return RV_ERR_INVAL;
    }

    switch (kind) {
    case RV_CD_RESOURCE_TEXTURE:
        return resident_texture_(resname, texture_out);
    case RV_CD_RESOURCE_AUDIO:
        return resident_audio_(resname, audio_out);
    case RV_CD_RESOURCE_MODEL:
        return resident_model_(resname, model_out);
    }

    // Neither kind this contract knows about: the same malformed-argument
    // refusal a bad handle or a short buffer gets elsewhere in this contract.
    // A fourth kind adds its own branch here, not a rewrite of the five
    // callers below.
    return RV_ERR_INVAL;
}

int64_t rv_pccd_fs::resident_texture_(const char *resname, texture_record *&texture_out)
{
    // No video attached is the same situation as no medium mounted: a
    // legal machine state, not a caller error, so it answers the way
    // asset_open answers an unmounted drive - nothing can be made
    // resident yet.
    if (cv_ == nullptr) {
        return RV_ERR_INVAL;
    }

    const std::string key(resname);
    const auto it = tex_by_name_.find(key);
    if (it != tex_by_name_.end()) {
        texture_out = &textures_[static_cast<size_t>(it->second)];
        return RV_OK;
    }
    return texture_resolve_(resname, texture_out);
}

int64_t rv_pccd_fs::resident_audio_(const char *resname, audio_record *&audio_out)
{
    // Same reasoning as cv_ above, for the sound side of the machine.
    if (ca_ == nullptr) {
        return RV_ERR_INVAL;
    }

    const std::string key(resname);
    const auto it = audio_by_name_.find(key);
    if (it != audio_by_name_.end()) {
        audio_out = &audios_[static_cast<size_t>(it->second)];
        return RV_OK;
    }
    return audio_resolve_(resname, audio_out);
}

// Cache-miss half of resource_resolve_() for TEXTURE: this is where a disc's
// "first ask" for a texture name becomes resident - read, decode, upload,
// and remember it for every ask that follows, including the ones made
// through a different one of the five query functions below. The cache-hit
// case never reaches here; see resource_resolve_().
int64_t rv_pccd_fs::texture_resolve_(const char *resname, texture_record *&record_out)
{
    std::vector<std::byte> bytes;
    const int64_t read_rc = asset_read_bytes_(resname, bytes);
    if (read_rc < 0) {
        return read_rc;
    }

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

    texture_record record;
    record.resname = resname;
    record.tex_addr = tex_addr;
    record.pal_addr = pal_addr;
    record.width = header.width;
    record.height = header.height;

    const int64_t index = static_cast<int64_t>(textures_.size());
    try {
        std::string key = record.resname;
        textures_.push_back(std::move(record));
        tex_by_name_[std::move(key)] = index;
    } catch (const std::bad_alloc &) {
        video_asset_release_(tex_addr);
        video_asset_release_(pal_addr);
        if (static_cast<int64_t>(textures_.size()) > index) {
            textures_.pop_back();
        }
        return RV_ERR_NOMEM;
    }

    record_out = &textures_[static_cast<size_t>(index)];
    return RV_OK;
}

// Cache-miss half of resource_resolve_() for AUDIO: a disc's "first ask" for
// a sound name becomes resident here. Unlike a texture there is nothing to
// decode - the PR review's decision on scope was raw PCM with no baker and
// no container - so this is read, upload, remember, three steps instead of
// four.
int64_t rv_pccd_fs::audio_resolve_(const char *resname, audio_record *&record_out)
{
    std::vector<std::byte> bytes;
    const int64_t read_rc = asset_read_bytes_(resname, bytes);
    if (read_rc < 0) {
        return read_rc;
    }

    const int64_t size = static_cast<int64_t>(bytes.size());
    const int64_t addr = ca_->sound_asset_malloc(size);
    if (addr < 0) {
        return addr;
    }

    rv_sample sample{};
    sample.data = bytes.data();
    sample.size = size;
    const int64_t write_rc = ca_->sound_asset_write(addr, &sample);
    if (write_rc < 0) {
        ca_->sound_asset_free(addr);
        return write_rc;
    }

    audio_record record;
    record.resname = resname;
    record.addr = addr;
    record.size = size;

    const int64_t index = static_cast<int64_t>(audios_.size());
    try {
        std::string key = record.resname;
        audios_.push_back(std::move(record));
        audio_by_name_[std::move(key)] = index;
    } catch (const std::bad_alloc &) {
        ca_->sound_asset_free(addr);
        if (static_cast<int64_t>(audios_.size()) > index) {
            audios_.pop_back();
        }
        return RV_ERR_NOMEM;
    }

    record_out = &audios_[static_cast<size_t>(index)];
    return RV_OK;
}

} // namespace rv_3dmppc
