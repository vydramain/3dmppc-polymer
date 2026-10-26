#include "example-cpp-bare.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace example_cpp_bare
{

namespace
{

// Log tag of the sound service.
constexpr const char *sound_tag = "sound";

// RV_ERR_INVAL as the integer type of the drive and ca calls, so the ternaries below have one type.
constexpr int64_t err_inval = RV_ERR_INVAL;

// The entry for `name`, added on first use.
rv_bare_sound_asset &sound_asset_entry(rv_bare_sound_state &state, const char *name)
{
    for (rv_bare_sound_asset &asset : state.assets) {
        if (asset.name == name) {
            return asset;
        }
    }
    rv_bare_sound_asset &asset = state.assets.emplace_back();
    asset.name = name;
    return asset;
}

// Logs the asset's first failure only: a retry that fails again would repeat the same line every frame.
void sound_asset_fail(rv_bare_sound_asset &asset, int64_t code)
{
    if (asset.logged) {
        return;
    }
    asset.logged = true;
    RV_LOG_ERR(sound_tag, "sample {} not resident: rv_err {}", asset.name, code);
}

// Maps a console call's result to RV_OK when it is not negative, else to its rv_err.
int sound_voice_result(int64_t code)
{
    return code < 0 ? static_cast<int>(code) : RV_OK;
}

} // namespace

int rv_bare_sound_resident(rv_cd *cd, rv_bare_sound_state &state, const char *name, int64_t &addr)
{
    if (!name) {
        RV_LOG_ERR(sound_tag, "sample name is null: rv_err {}", static_cast<int>(RV_ERR_INVAL));
        return RV_ERR_INVAL;
    }
    rv_bare_sound_asset &asset = sound_asset_entry(state, name);
    if (asset.resident) {
        addr = asset.addr;
        return RV_OK;
    }
    const int64_t drive_addr = cd ? rv_cd_resource_addr(cd, RV_CD_RESOURCE_AUDIO, name) : err_inval;
    if (drive_addr < 0) {
        sound_asset_fail(asset, drive_addr);
        return static_cast<int>(drive_addr);
    }
    asset.addr = drive_addr;
    asset.resident = true;
    addr = drive_addr;
    return RV_OK;
}

int rv_bare_sound_voice_setup(rv_ca *ca, const rv_voice_conf &conf)
{
    return sound_voice_result(ca ? rv_ca_voice_setup(ca, &conf) : err_inval);
}

int rv_bare_sound_voice_play(rv_ca *ca, int64_t voice)
{
    return sound_voice_result(ca ? rv_ca_voice_play(ca, voice) : err_inval);
}

int rv_bare_sound_voice_stop(rv_ca *ca, int64_t voice)
{
    return sound_voice_result(ca ? rv_ca_voice_stop(ca, voice) : err_inval);
}

} // namespace example_cpp_bare
