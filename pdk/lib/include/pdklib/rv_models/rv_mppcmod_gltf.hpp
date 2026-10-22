// glTF 2.0 reading, compiled by mppcmodbaker. Never the console:
// cgltf is not on its include path, and cmake/pdklib.cmake keeps these files out of 3dmppc_pdklib.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_quant.hpp"

namespace rv_pdklib
{

// A PNG the model uses, embedded or named by a relative URI. `name` is <stem>-<glTF image index>; the texture of a part
// is `<name>.mppctex`, and the CLI writes the PNG as `<name>.png`.
struct rv_mppcmod_gltf_image {
    std::string name;
    std::vector<uint8_t> png;
};

// Flattens every mesh node of `path` into `model`: one part per primitive, moved by the
// node's world matrix. Each glTF number goes to Q.16 once; the composition is int64. Each PNG
// a part uses is appended to `images` once.
// Returns RV_ERR_INVAL with `error` set to one sentence naming the cause, prefixed by the path.
int rv_mppcmod_read_gltf(const std::string &path,
    const std::string &stem,
    rv_mppcmod_q16_model &model,
    std::vector<rv_mppcmod_gltf_image> &images,
    std::string &error);

// TRANSITIONAL: the float model for the editor. Same reading; positions and uvs divided by 2^16.
// Goes away with its last caller.
int rv_mppcmod_read_gltf(const std::string &path,
    const std::string &stem,
    rv_mppcmod &model,
    std::vector<rv_mppcmod_gltf_image> &images,
    std::string &error);

} // namespace rv_pdklib
