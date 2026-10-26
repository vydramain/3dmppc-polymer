#include "example-cpp-bare-layout.hpp"

// Taken once from mppcdiscs/example-cpp-pdkkit/scenes/main.scene.toml as of b11d7243, before b9f62b8e moved the
// house, apartment, furniture and cameras: world is rv_gte_trs T*Ry*Rx*Rz*S,
// composed over each parent (rv_editor_scene_world_gte). Records follow scene file order. The scene sets no tint,
// so every tint is white. Models are the disc resource names of the sources (rv_mppcmod_disc_name).
// 4096 is 1.0 in r (4.12) and one metre in t (Q20.12).

namespace example_cpp_bare
{

namespace
{

// The scene sets no tint, so each model is drawn uncoloured: white is the neutral multiplier.
constexpr rv_color RV_EXAMPLE_CPP_BARE_WHITE = { 255, 255, 255 };

constexpr bare_mesh BARE_MESHES[] = {
    { "factory.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 0, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "board.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57098, 17613, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "street_16.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 0, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "house_7.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 0, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "apartment.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 973, -6528 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "lamp.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 16384, 0, 14746 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "lamp.mppcmod", { { -4096, 0, 0, 0, 4096, 0, 0, 0, -4096 }, { 49152, 0, -14746 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "lamp.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 81920, 0, 14746 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "lamp.mppcmod", { { -4096, 0, 0, 0, 4096, 0, 0, 0, -4096 }, { 114688, 0, -14746 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "machines.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 0, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "furniture.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 973, -6528 } }, RV_EXAMPLE_CPP_BARE_WHITE },
    { "yard.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 0, 0, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE },
};

constexpr bare_lamp BARE_LAMPS[] = {
    { "lamp_light.mppcmod",
        { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 16384, 0, 14746 } },
        RV_EXAMPLE_CPP_BARE_WHITE,
        { 16384, 0, 14746 } },
    { "lamp_light.mppcmod",
        { { -4096, 0, 0, 0, 4096, 0, 0, 0, -4096 }, { 49152, 0, -14746 } },
        RV_EXAMPLE_CPP_BARE_WHITE,
        { 49152, 0, -14746 } },
    { "lamp_light.mppcmod",
        { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { 81920, 0, 14746 } },
        RV_EXAMPLE_CPP_BARE_WHITE,
        { 81920, 0, 14746 } },
    { "lamp_light.mppcmod",
        { { -4096, 0, 0, 0, 4096, 0, 0, 0, -4096 }, { 114688, 0, -14746 } },
        RV_EXAMPLE_CPP_BARE_WHITE,
        { 114688, 0, -14746 } },
};

constexpr bare_decal BARE_DECALS[] = {
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, -4915 } }, RV_EXAMPLE_CPP_BARE_WHITE, 0 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, -3932 } }, RV_EXAMPLE_CPP_BARE_WHITE, 1 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, -2949 } }, RV_EXAMPLE_CPP_BARE_WHITE, 2 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, -1966 } }, RV_EXAMPLE_CPP_BARE_WHITE, 3 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, -983 } }, RV_EXAMPLE_CPP_BARE_WHITE, 4 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE, 5 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, 983 } }, RV_EXAMPLE_CPP_BARE_WHITE, 6 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, 1966 } }, RV_EXAMPLE_CPP_BARE_WHITE, 7 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, 2949 } }, RV_EXAMPLE_CPP_BARE_WHITE, 8 },
    { "halo.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57078, 17613, 4915 } }, RV_EXAMPLE_CPP_BARE_WHITE, 10 },
    { "glyph_o.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, -4915 } }, RV_EXAMPLE_CPP_BARE_WHITE, 0 },
    { "glyph_s.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, -3932 } }, RV_EXAMPLE_CPP_BARE_WHITE, 1 },
    { "glyph_t.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, -2949 } }, RV_EXAMPLE_CPP_BARE_WHITE, 2 },
    { "glyph_a.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, -1966 } }, RV_EXAMPLE_CPP_BARE_WHITE, 3 },
    { "glyph_l.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, -983 } }, RV_EXAMPLE_CPP_BARE_WHITE, 4 },
    { "glyph_o.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, 0 } }, RV_EXAMPLE_CPP_BARE_WHITE, 5 },
    { "glyph_s.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, 983 } }, RV_EXAMPLE_CPP_BARE_WHITE, 6 },
    { "glyph_soft.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, 1966 } }, RV_EXAMPLE_CPP_BARE_WHITE, 7 },
    { "glyph_colon.mppcmod",
        { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, 2949 } },
        RV_EXAMPLE_CPP_BARE_WHITE,
        8 },
    { "glyph_5.mppcmod", { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -57057, 17613, 4915 } }, RV_EXAMPLE_CPP_BARE_WHITE, 10 },
};

constexpr bare_camera BARE_CAMERAS[] = {
    { 0, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { -36864, 17203, 0 } } },
    { 1, { { 0, -144, -4093, 0, 4093, -144, 4096, 0, 0 }, { -32768, 16384, 2458 } } },
    { 2, { { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 }, { -26624, 13926, 6144 } } },
    { 3, { { 0, 0, 4096, 0, 4096, 0, -4096, 0, 0 }, { -12288, 8192, 0 } } },
    { 4, { { 0, 0, 4096, 0, 4096, 0, -4096, 0, 0 }, { -2048, 6963, 0 } } },
    { 5, { { 0, 0, 4096, 0, 4096, 0, -4096, 0, 0 }, { 12288, 7782, 0 } } },
    { 6, { { 0, 0, 4096, 0, 4096, 0, -4096, 0, 0 }, { 49152, 11469, 0 } } },
    { 7, { { 0, -214, 4090, 0, 4090, 214, -4096, 0, 0 }, { 98304, 16384, 0 } } },
    { 8, { { 0, -144, 4093, 0, 4093, 144, -4096, 0, 0 }, { 124928, 29645, -6528 } } },
    { 9, { { 0, 0, 4096, 0, 4096, 0, -4096, 0, 0 }, { 135168, 31488, -6528 } } },
    { 10, { { 0, 358, 4080, 0, 4080, -358, -4096, 0, 0 }, { 144589, 31283, -6528 } } },
    { 11, { { -4096, 0, 0, 0, 4034, -713, 0, -713, -4034 }, { 149914, 31283, -4480 } } },
    { 12, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { 143360, 31488, -6528 } } },
    { 13, { { 0, -214, -4090, 0, 4090, -214, 4096, 0, 0 }, { 133120, 30464, -6528 } } },
    { 14, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { 110592, 20480, 0 } } },
    { 15, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { 73728, 13107, 0 } } },
    { 16, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { 36864, 9830, 0 } } },
    { 17, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { 8192, 7373, 0 } } },
    { 18, { { 0, 0, -4096, 0, 4096, 0, 4096, 0, 0 }, { -6144, 7373, 0 } } },
    { 19, { { 0, 214, -4090, 0, 4090, 214, 4096, 0, 0 }, { -20480, 12288, 0 } } },
};

} // namespace

std::span<const bare_mesh> bare_meshes()
{
    return BARE_MESHES;
}

std::span<const bare_lamp> bare_lamps()
{
    return BARE_LAMPS;
}

std::span<const bare_decal> bare_decals()
{
    return BARE_DECALS;
}

std::span<const bare_camera> bare_cameras()
{
    return BARE_CAMERAS;
}

} // namespace example_cpp_bare
