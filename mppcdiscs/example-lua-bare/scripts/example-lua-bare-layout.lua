-- The scene objects of main.scene.toml that the disc uses, in scene order: the meshes (kind "mesh",
-- the board and the lamps among them) and the cam_NN cameras (kind "camera"). The group `route`
-- is not used by the disc and is left out. Taken ONCE from scenes/main.scene.toml with the
-- console's scene reader; the disc no longer reads the scene at run time.
-- Each world number is written as %.17g: it reads back in Lua as the same double the disc got from
-- object.world[k] (a float32 widened to double), so the frames stay byte-equal. Tints are uint8
-- values, written as integers. Row-major, 1-based, as object.world was read.
-- The order is the draw order and the lamp order; keep it.
return {
    {
        name = "factory",
        kind = "mesh",
        model = "factory.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "board",
        kind = "mesh",
        model = "board.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, -13.93994140625,
            0, 1, 0, 4.300048828125,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "street",
        kind = "mesh",
        model = "street.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "house",
        kind = "mesh",
        model = "house.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "apartment",
        kind = "mesh",
        model = "apartment.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0.237548828125,
            0, 0, 1, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "lamp_1",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 4,
            0, 1, 0, 0,
            0, 0, 1, 3.60009765625,
            0, 0, 0, 1,
        },
    },
    {
        name = "lamp_2",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            -1, 0, 0, 12,
            0, 1, 0, 0,
            0, 0, -1, -3.60009765625,
            0, 0, 0, 1,
        },
    },
    {
        name = "lamp_3",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 20,
            0, 1, 0, 0,
            0, 0, 1, 3.60009765625,
            0, 0, 0, 1,
        },
    },
    {
        name = "lamp_4",
        kind = "mesh",
        model = "lamp.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            -1, 0, 0, 28,
            0, 1, 0, 0,
            0, 0, -1, -3.60009765625,
            0, 0, 0, 1,
        },
    },
    {
        name = "machines",
        kind = "mesh",
        model = "machines.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "furniture",
        kind = "mesh",
        model = "furniture.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0.237548828125,
            0, 0, 1, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "yard",
        kind = "mesh",
        model = "yard.mppcmod",
        tint = { r = 255, g = 255, b = 255 },
        world = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_00",
        kind = "camera",
        world = {
            0, 0, -1, -9,
            0, 1, 0, 4.199951171875,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_01",
        kind = "camera",
        world = {
            0, -0.034899495542049408, -0.99939084053039551, -8,
            0, 0.99939084053039551, -0.034899495542049408, 4,
            1, 0, 0, 0.60009765625,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_02",
        kind = "camera",
        world = {
            1, 0, 0, -6.5,
            0, 1, 0, 3.39990234375,
            0, 0, 1, 1.5,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_03",
        kind = "camera",
        world = {
            0, 0, 1, -3,
            0, 1, 0, 2,
            -1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_04",
        kind = "camera",
        world = {
            0, 0, 1, -0.5,
            0, 1, 0, 1.699951171875,
            -1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_05",
        kind = "camera",
        world = {
            0, 0, 1, 3,
            0, 1, 0, 1.89990234375,
            -1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_06",
        kind = "camera",
        world = {
            0, 0, 1, 12,
            0, 1, 0, 2.800048828125,
            -1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_07",
        kind = "camera",
        world = {
            0, -0.052335958927869797, 0.99862951040267944, 24,
            0, 0.99862951040267944, 0.052335958927869797, 4,
            -1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_08",
        kind = "camera",
        world = {
            0, -0.034899495542049408, 0.99939084053039551, 30.5,
            0, 0.99939084053039551, 0.034899495542049408, 7.237548828125,
            -1, 0, 0, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_09",
        kind = "camera",
        world = {
            0, 0, 1, 33,
            0, 1, 0, 7.6875,
            -1, 0, 0, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_10",
        kind = "camera",
        world = {
            0, 0.087155736982822418, 0.99619472026824951, 35.300048828125,
            0, 0.99619472026824951, -0.087155736982822418, 7.637451171875,
            -1, 0, 0, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_11",
        kind = "camera",
        world = {
            -1, 0, 0, 36.60009765625,
            0, 0.98480772972106934, -0.1736481785774231, 7.637451171875,
            0, -0.1736481785774231, -0.98480772972106934, -1.09375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_12",
        kind = "camera",
        world = {
            0, 0, -1, 35,
            0, 1, 0, 7.6875,
            1, 0, 0, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_13",
        kind = "camera",
        world = {
            0, -0.052335958927869797, -0.99862951040267944, 32.5,
            0, 0.99862951040267944, -0.052335958927869797, 7.4375,
            1, 0, 0, -1.59375,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_14",
        kind = "camera",
        world = {
            0, 0, -1, 27,
            0, 1, 0, 5,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_15",
        kind = "camera",
        world = {
            0, 0, -1, 18,
            0, 1, 0, 3.199951171875,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_16",
        kind = "camera",
        world = {
            0, 0, -1, 9,
            0, 1, 0, 2.39990234375,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_17",
        kind = "camera",
        world = {
            0, 0, -1, 2,
            0, 1, 0, 1.800048828125,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_18",
        kind = "camera",
        world = {
            0, 0, -1, -1.5,
            0, 1, 0, 1.800048828125,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
    {
        name = "cam_19",
        kind = "camera",
        world = {
            0, 0.052335958927869797, -0.99862951040267944, -5,
            0, 0.99862951040267944, 0.052335958927869797, 3,
            1, 0, 0, 0,
            0, 0, 0, 1,
        },
    },
}
