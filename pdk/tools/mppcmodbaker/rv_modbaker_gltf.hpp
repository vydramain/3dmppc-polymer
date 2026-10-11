// mppcmodbaker: reads a glTF 2.0 model (.glb or .gltf) into the .mppcmod container.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"

// A PNG the model embeds. `name` is <stem>-<glTF image index>; the texture of a part
// is `<name>.mppctex`, and the CLI writes the PNG as `<name>.png`.
struct rv_modbaker_image {
    std::string name;
    std::vector<uint8_t> png;
};

// Flattens every mesh node of `path` into `model`: one part per primitive, moved by the
// node's world matrix. Each embedded PNG a part uses is appended to `images` once.
// Returns RV_ERR_INVAL with `error` set to one sentence naming the cause, prefixed by the path.
int rv_modbaker_read_gltf(const std::string &path,
    const std::string &stem,
    rv_pdklib::rv_mppcmod &model,
    std::vector<rv_modbaker_image> &images,
    std::string &error);
