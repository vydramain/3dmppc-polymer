-- Puts one model at a row-major world matrix in a depth window, culled back: the two puts
-- every draw of the disc goes through (rv_draw_model and rv_draw_decal in example-cpp-draw.hpp).
local M = {}

local MATRIX_LEN = require("example-lua-math").MATRIX_LEN

-- The ordering-table windows, as example-cpp-draw.hpp: the decal window is the world window
-- DECAL_BIAS keys nearer, same width, so a decal wins over the wall it lies on.
local DECAL_BIAS = 260
local WORLD_DEPTH_MIN = pdk.DEPTH_MIN
local WORLD_DEPTH_MAX = pdk.DEPTH_MAX - DECAL_BIAS
local DECAL_DEPTH_MIN = pdk.DEPTH_MIN + DECAL_BIAS
local DECAL_DEPTH_MAX = pdk.DEPTH_MAX

-- Models are not tinted by the lamps and the board: SAMPLE_TEXTURE replaces the vertex
-- colour with the texel. A mesh passes its scene tint instead.
M.WHITE = { r = 255, g = 255, b = 255 }

-- world is a 1-based Lua table of 16 numbers. Returns 0 or the first drive or cg error.
local function put_window(cg, cd, name, world, tint, depth_min, depth_max)
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_MODEL, name))
    if addr < 0 then
        return addr
    end
    local put = pdk.new("rv_cg_put")
    for k = 0, MATRIX_LEN - 1 do
        put.world[k] = world[k + 1]
    end
    put.depth_min = depth_min
    put.depth_max = depth_max
    put.cull = pdk.CG_CULL_BACK
    put.tint.r, put.tint.g, put.tint.b = tint.r, tint.g, tint.b
    return tonumber(pdk.cg_model_put(cg, addr, put))
end

-- A world model (the lamps, the scene meshes).
function M.put(cg, cd, name, world, tint)
    return put_window(cg, cd, name, world, tint, WORLD_DEPTH_MIN, WORLD_DEPTH_MAX)
end

-- A decal on a wall (the board halos and letters).
function M.decal(cg, cd, name, world, tint)
    return put_window(cg, cd, name, world, tint, DECAL_DEPTH_MIN, DECAL_DEPTH_MAX)
end

return M
