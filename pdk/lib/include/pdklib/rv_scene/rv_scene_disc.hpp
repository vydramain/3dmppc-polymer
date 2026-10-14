#pragma once

// rv_scene_load opens a disc asset by name and parses it as a scene, so a disc
// does not repeat read_asset's open/size/read dance around rv_scene_parse. The
// parser itself stays pure: this header only reads bytes and hands them over.
// A disc using it still includes rv_scene_unit.hpp once, for the parser's code.

#include <cstddef>
#include <string>

#include "pdk/cd/rv_cd.h"
#include "pdklib/rv_scene/rv_scene.hpp"

namespace rv_pdklib
{

inline int rv_scene_load(rv_cd *cd, const char *name, rv_scene &scene, std::string &error)
{
    const int64_t handle = cd ? rv_cd_asset_open(cd, name) : -1;
    if (handle < 0) {
        error = std::string(name) + " is not on the disc";
        return 1;
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        error = std::string(name) + " could not be read from the disc";
        return 1;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    const int64_t read = rv_cd_asset_read(cd, handle, bytes.data(), size);
    if (read < 0) {
        error = std::string(name) + " could not be read from the disc";
        return 1;
    }
    bytes.resize(static_cast<std::size_t>(read));
    return rv_scene_parse(bytes, name, scene, error);
}

} // namespace rv_pdklib
