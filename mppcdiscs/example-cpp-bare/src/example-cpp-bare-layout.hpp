#pragma once

#include <cstdint>
#include <span>

#include "pdk/cv/rv_vertex.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_bare
{

// Scene objects of main.scene.toml, taken once: world matrices are composed from the scene's transforms.
// Draw order is bare_meshes, then bare_lamps, then bare_decals; each span is in scene file order.

// A mesh with no drive of its own. model is the disc's resource name, NUL-terminated ("factory.mppcmod").
struct bare_mesh {
    const char *model;
    rv_pdklib::rv_gte_matrix world;
    rv_color tint;
};

// A lamp_N_light: shown while the route passes its lamp, decided per frame. base is the world translation of the
// lamp_N mesh the light hangs from, Q20.12: the schedule measures the route's eye from it.
struct bare_lamp {
    const char *model;
    rv_pdklib::rv_gte_matrix world;
    rv_color tint;
    rv_pdklib::fx_vec3 base;
};

// A halo_N or glyph_N, drawn with the decal depth window: shown while its board cell is lit. cell is 0..10, never 9
// (the space between colon and digit has no entity).
struct bare_decal {
    const char *model;
    rv_pdklib::rv_gte_matrix world;
    rv_color tint;
    int32_t cell;
};

// A cam_NN key of the route: index is NN, the key order. The eye is world t; the view direction is the z column of world r.
struct bare_camera {
    int32_t index;
    rv_pdklib::rv_gte_matrix world;
};

std::span<const bare_mesh> bare_meshes();
std::span<const bare_lamp> bare_lamps();
std::span<const bare_decal> bare_decals();
std::span<const bare_camera> bare_cameras();

} // namespace example_cpp_bare
