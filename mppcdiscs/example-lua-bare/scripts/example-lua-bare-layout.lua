-- The scene objects of main.scene.toml that the disc uses, in scene order: the meshes (kind "mesh",
-- the board and the lamps among them) and the cam_NN cameras (kind "camera"). Other scene objects
-- (route, *_light, halo_*, glyph_*) are not in this table.
-- Taken ONCE from scenes/main.scene.toml with the editor's rv_editor_scene_world_gte: the integer
-- chain of pdkkit's ecs_transform_world (rv_gte_trs T*Ry*Rx*Rz*S, then each ancestor's matrix
-- composed outside it). The disc no longer reads the scene at run time.
-- world.r: the 3x3 rotation and scale, row-major, 4.12 (4096 = 1.0). world.t: the translation, Q20.12
-- (4096 = 1 unit). Lua arrays are 1-based. Tints are uint8 values.
-- The order is the draw order and the lamp order; keep it.
return {
    {
        name = "factory",
        kind = "mesh",
        model = "factory.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 0, 0 },
        },
    },
    {
        name = "board",
        kind = "mesh",
        model = "board.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { -57098, 17613, 0 },
        },
    },
    {
        name = "street",
        kind = "mesh",
        model = "street_16.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 0, 0 },
        },
    },
    {
        name = "house",
        kind = "mesh",
        model = "house_7.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 0, 0 },
        },
    },
    {
        name = "apartment",
        kind = "mesh",
        model = "apartment.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 973, -6528 },
        },
    },
    {
        name = "lamp_1",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 16384, 0, 14746 },
        },
    },
    {
        name = "lamp_2",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                -4096, 0, 0,
                0, 4096, 0,
                0, 0, -4096,
            },
            t = { 49152, 0, -14746 },
        },
    },
    {
        name = "lamp_3",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 81920, 0, 14746 },
        },
    },
    {
        name = "lamp_4",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                -4096, 0, 0,
                0, 4096, 0,
                0, 0, -4096,
            },
            t = { 114688, 0, -14746 },
        },
    },
    {
        name = "machines",
        kind = "mesh",
        model = "machines.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 0, 0 },
        },
    },
    {
        name = "furniture",
        kind = "mesh",
        model = "furniture.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 973, -6528 },
        },
    },
    {
        name = "yard",
        kind = "mesh",
        model = "yard.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { 0, 0, 0 },
        },
    },
    {
        name = "cam_00",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { -36864, 17203, 0 },
        },
    },
    {
        name = "cam_01",
        kind = "camera",
        world = {
            r = {
                0, -144, -4093,
                0, 4093, -144,
                4096, 0, 0,
            },
            t = { -32768, 16384, 2458 },
        },
    },
    {
        name = "cam_02",
        kind = "camera",
        world = {
            r = {
                4096, 0, 0,
                0, 4096, 0,
                0, 0, 4096,
            },
            t = { -26624, 13926, 6144 },
        },
    },
    {
        name = "cam_03",
        kind = "camera",
        world = {
            r = {
                0, 0, 4096,
                0, 4096, 0,
                -4096, 0, 0,
            },
            t = { -12288, 8192, 0 },
        },
    },
    {
        name = "cam_04",
        kind = "camera",
        world = {
            r = {
                0, 0, 4096,
                0, 4096, 0,
                -4096, 0, 0,
            },
            t = { -2048, 6963, 0 },
        },
    },
    {
        name = "cam_05",
        kind = "camera",
        world = {
            r = {
                0, 0, 4096,
                0, 4096, 0,
                -4096, 0, 0,
            },
            t = { 12288, 7782, 0 },
        },
    },
    {
        name = "cam_06",
        kind = "camera",
        world = {
            r = {
                0, 0, 4096,
                0, 4096, 0,
                -4096, 0, 0,
            },
            t = { 49152, 11469, 0 },
        },
    },
    {
        name = "cam_07",
        kind = "camera",
        world = {
            r = {
                0, -214, 4090,
                0, 4090, 214,
                -4096, 0, 0,
            },
            t = { 98304, 16384, 0 },
        },
    },
    {
        name = "cam_08",
        kind = "camera",
        world = {
            r = {
                0, -144, 4093,
                0, 4093, 144,
                -4096, 0, 0,
            },
            t = { 124928, 29645, -6528 },
        },
    },
    {
        name = "cam_09",
        kind = "camera",
        world = {
            r = {
                0, 0, 4096,
                0, 4096, 0,
                -4096, 0, 0,
            },
            t = { 135168, 31488, -6528 },
        },
    },
    {
        name = "cam_10",
        kind = "camera",
        world = {
            r = {
                0, 358, 4080,
                0, 4080, -358,
                -4096, 0, 0,
            },
            t = { 144589, 31283, -6528 },
        },
    },
    {
        name = "cam_11",
        kind = "camera",
        world = {
            r = {
                -4096, 0, 0,
                0, 4034, -713,
                0, -713, -4034,
            },
            t = { 149914, 31283, -4480 },
        },
    },
    {
        name = "cam_12",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { 143360, 31488, -6528 },
        },
    },
    {
        name = "cam_13",
        kind = "camera",
        world = {
            r = {
                0, -214, -4090,
                0, 4090, -214,
                4096, 0, 0,
            },
            t = { 133120, 30464, -6528 },
        },
    },
    {
        name = "cam_14",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { 110592, 20480, 0 },
        },
    },
    {
        name = "cam_15",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { 73728, 13107, 0 },
        },
    },
    {
        name = "cam_16",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { 36864, 9830, 0 },
        },
    },
    {
        name = "cam_17",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { 8192, 7373, 0 },
        },
    },
    {
        name = "cam_18",
        kind = "camera",
        world = {
            r = {
                0, 0, -4096,
                0, 4096, 0,
                4096, 0, 0,
            },
            t = { -6144, 7373, 0 },
        },
    },
    {
        name = "cam_19",
        kind = "camera",
        world = {
            r = {
                0, 214, -4090,
                0, 4090, 214,
                4096, 0, 0,
            },
            t = { -20480, 12288, 0 },
        },
    },
}
