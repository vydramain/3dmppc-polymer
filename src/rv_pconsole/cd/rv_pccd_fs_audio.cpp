// rv_pccd_fs AUDIO residency: a sound read from the disc, uploaded to sound RAM
// through the sound controller and remembered. Kept out of rv_pccd_fs.cpp so it
// stays under its 512-line limit.
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "pdk/ca/rv_sample.h"
#include "pdk/rv_err.h"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

namespace rv_3dmppc
{

int64_t rv_pccd_fs::resident_audio_(const char *resname, audio_record *&audio_out)
{
    // No sound controller (ca_) attached: nowhere to make the sound resident.
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
