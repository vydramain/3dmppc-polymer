// glTF image reading for rv_mppcmod_gltf.cpp. Compiled by mppcmodbaker, never by the console.
#pragma once

#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod_gltf.hpp"

// Forward declarations keep cgltf.h out of this header: only .cpp files that need its
// definitions include it, so cgltf's types do not leak into every file that includes this.
struct cgltf_data;
struct cgltf_material;

namespace rv_pdklib::detail
{

// What the texture walk needs of one file: its parsed data, the names its PNGs take, the directory its
// image files are read from, and the list its PNGs are kept in. `image_seen` marks the glTF images already kept, by index.
struct image_sink {
    const cgltf_data *data = nullptr;
    std::string stem;
    std::string dir;
    std::vector<rv_mppcmod_gltf_image> *images = nullptr;
    std::vector<bool> image_seen;
};

// Every buffer and image URI must pass is_allowed_uri; the error names the first that does not.
int check_uris(const cgltf_data *data, std::string &error);

// Names the texture of `material` and keeps its PNG once. Leaves `texture` empty when
// the material has none.
int bake_texture(image_sink &sink,
    const cgltf_material *material,
    const std::string &where,
    std::string &texture,
    std::string &error);

} // namespace rv_pdklib::detail
