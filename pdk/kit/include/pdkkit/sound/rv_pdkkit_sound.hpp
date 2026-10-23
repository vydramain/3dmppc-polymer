#pragma once

// Sound for a disc's own code: a named sample made resident by the drive, and the voice setup, play and stop calls.
// The drive owns the sample's sound RAM (rv_cd.h): this service never reads, allocates or frees it.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

inline constexpr const char *sound_tag = "sound";

// Samples the disc can make resident through this service: a fixed table, no heap.
// A disc keeps few samples resident here (short effects and loops), and 8 covers that with room to spare.
inline constexpr int sound_asset_capacity = 8;

// One named sample the disc has asked for. The address is the drive's and is valid until the disc unloads.
// The name is NUL-terminated within ecs_name_capacity bytes, the same bound as the names of the scene records
// (rv_component_capacity.hpp: object, entity, component).
struct sound_asset {
    char name[ecs_name_capacity]{};
    int64_t addr = 0;
    bool resident = false;
    // The first failure is logged; a later failure of the same asset is silent.
    bool logged = false;
};

// Owned by the disc. A disc's samples are few, so a linear search by name is enough.
struct sound_state {
    sound_asset assets[sound_asset_capacity]{};
    int count = 0;
};

static_assert(std::is_standard_layout_v<sound_asset> && std::is_trivially_copyable_v<sound_asset>);
static_assert(std::is_standard_layout_v<sound_state> && std::is_trivially_copyable_v<sound_state>);

// Points `asset` at the entry for `name`, added on first use. A name too long for the table, or a full table, is
// refused with a log on every call, as a null name is: there is no entry to dedupe on.
[[nodiscard]] inline int sound_asset_entry(sound_state &state, const char *name, sound_asset *&asset)
{
    const std::size_t length = std::strlen(name);
    if (length >= ecs_name_capacity) {
        RV_LOG_ERR(sound_tag,
            "sample name longer than {} bytes: rv_err {}",
            ecs_name_capacity - 1,
            static_cast<int>(RV_ERR_INVAL));
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < state.count; ++i) {
        if (std::strcmp(state.assets[i].name, name) == 0) {
            asset = &state.assets[i];
            return RV_OK;
        }
    }
    if (state.count == sound_asset_capacity) {
        RV_LOG_ERR(sound_tag,
            "sample table full ({} entries): rv_err {}",
            sound_asset_capacity,
            static_cast<int>(RV_ERR_NOMEM));
        return RV_ERR_NOMEM;
    }
    asset = &state.assets[state.count];
    ++state.count;
    std::memcpy(asset->name, name, length + 1);
    return RV_OK;
}

// Logs the asset's first failure only: a retry that fails again would repeat the same line every frame.
inline void sound_asset_fail(sound_asset &asset, int64_t code)
{
    if (asset.logged) {
        return;
    }
    asset.logged = true;
    RV_LOG_ERR(sound_tag, "sample {} not resident: rv_err {}", asset.name, code);
}

// Makes `name` resident and returns its sound RAM address in `addr` (set only on RV_OK). The first call is the
// drive's read and upload; later calls return the same address without a drive call. A failure returns the
// drive's rv_err, logged once per asset. A null name is a programming error the disc fixes, so it is logged on
// every call instead (it has no asset entry to dedupe on).
[[nodiscard]] inline int sound_asset_resident(rv_cd *cd, sound_state &state, const char *name, int64_t &addr)
{
    if (!name) {
        RV_LOG_ERR(sound_tag, "sample name is null: rv_err {}", static_cast<int>(RV_ERR_INVAL));
        return RV_ERR_INVAL;
    }
    sound_asset *asset = nullptr;
    const int entry_code = sound_asset_entry(state, name, asset);
    if (entry_code != RV_OK) {
        return entry_code;
    }
    if (asset->resident) {
        addr = asset->addr;
        return RV_OK;
    }
    const int64_t drive_addr = cd ? rv_cd_resource_addr(cd, RV_CD_RESOURCE_AUDIO, name) : static_cast<int64_t>(RV_ERR_INVAL);
    if (drive_addr < 0) {
        sound_asset_fail(*asset, drive_addr);
        return static_cast<int>(drive_addr);
    }
    asset->addr = drive_addr;
    asset->resident = true;
    addr = drive_addr;
    return RV_OK;
}

// Maps a console call's result to RV_OK when it is not negative, else to its rv_err. Unlogged: a voice call is
// retried every frame while it fails, so the caller logs the first failure, as it does for its sounds.
inline int sound_voice_result(int64_t code)
{
    return code < 0 ? static_cast<int>(code) : RV_OK;
}

// Sets up the voice from conf (sample address, loop, envelope, volumes). conf is passed through unchanged.
[[nodiscard]] inline int sound_voice_setup(rv_ca *ca, const rv_voice_conf &conf)
{
    return sound_voice_result(ca ? rv_ca_voice_setup(ca, &conf) : static_cast<int64_t>(RV_ERR_INVAL));
}

// Starts the voices in `voice` (a mask), as set up by sound_voice_setup.
[[nodiscard]] inline int sound_voice_play(rv_ca *ca, int64_t voice)
{
    return sound_voice_result(ca ? rv_ca_voice_play(ca, voice) : static_cast<int64_t>(RV_ERR_INVAL));
}

// Stops the voices in `voice` (a mask).
[[nodiscard]] inline int sound_voice_stop(rv_ca *ca, int64_t voice)
{
    return sound_voice_result(ca ? rv_ca_voice_stop(ca, voice) : static_cast<int64_t>(RV_ERR_INVAL));
}

} // namespace rv_pdkkit
