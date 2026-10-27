#pragma once

// Sound for a disc's own code: a named sample made resident by the drive, and the voice setup, play and stop calls.
// The drive owns the sample's sound RAM and its residency by name (rv_cd.h): this service keeps no table and never
// reads, allocates or frees sound RAM.

#include <cstdint>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

inline constexpr const char *sound_tag = "sound";

// Makes `name` resident through the drive and returns its sound RAM address in `addr` (set only on RV_OK). The drive
// keeps the sample resident by name, so every call asks it again; the drive logs its own refusal once per name, so a
// refusal is returned here without a log. A null cd or name has no drive to ask or no name to ask for: logged on every call.
[[nodiscard]] inline int sound_asset_resident(rv_cd *cd, const char *name, int64_t &addr)
{
    if (cd == nullptr) {
        RV_LOG_ERR(sound_tag, "no drive for sample: rv_err {}", static_cast<int>(RV_ERR_INVAL));
        return RV_ERR_INVAL;
    }
    if (name == nullptr) {
        RV_LOG_ERR(sound_tag, "sample name is null: rv_err {}", static_cast<int>(RV_ERR_INVAL));
        return RV_ERR_INVAL;
    }
    const int64_t drive_addr = rv_cd_resource_addr(cd, RV_CD_RESOURCE_AUDIO, name);
    if (drive_addr < 0) {
        return static_cast<int>(drive_addr);
    }
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
