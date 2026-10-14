// glTF 2.0 reading, compiled by mppcmodbaker. Never the console:
// cgltf is not on its include path, and cmake/pdklib.cmake keeps these files out of 3dmppc_pdklib.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"

namespace rv_pdklib
{

// A PNG the model uses, embedded or named by a relative URI. `name` is <stem>-<glTF image index>; the texture of a part
// is `<name>.mppctex`, and the CLI writes the PNG as `<name>.png`.
struct rv_mppcmod_gltf_image {
    std::string name;
    std::vector<uint8_t> png;
};

// Flattens every mesh node of `path` into `model`: one part per primitive, moved by the
// node's world matrix. Each PNG a part uses is appended to `images` once.
// Returns RV_ERR_INVAL with `error` set to one sentence naming the cause, prefixed by the path.
int rv_mppcmod_read_gltf(const std::string &path,
    const std::string &stem,
    rv_mppcmod &model,
    std::vector<rv_mppcmod_gltf_image> &images,
    std::string &error);

} // namespace rv_pdklib
