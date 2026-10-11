-- Puts one model at a row-major world matrix in the world window, culled back: the
-- one put every draw of the disc goes through (rv_draw_model in example-cpp-draw.hpp).
local M = {}

local MATRIX_LEN = require("example-lua-math").MATRIX_LEN

-- The ordering-table window of the world, as example-cpp-draw.hpp.
local WORLD_DEPTH_MIN = 0
local WORLD_DEPTH_MAX = 959

-- Models are not tinted by the lamps and the board: SAMPLE_TEXTURE replaces the vertex
-- colour with the texel. A mesh passes its scene tint instead.
M.WHITE = { r = 255, g = 255, b = 255 }

-- world is a 1-based Lua table of 16 numbers. Returns 0 or the first drive or cg error.
function M.put(cg, cd, name, world, tint)
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_MODEL, name))
    if addr < 0 then
        return addr
    end
    local put = pdk.new("rv_cg_put")
    for k = 0, MATRIX_LEN - 1 do
        put.world[k] = world[k + 1]
    end
    put.depth_min = WORLD_DEPTH_MIN
    put.depth_max = WORLD_DEPTH_MAX
    put.cull = pdk.CG_CULL_BACK
    put.tint.r, put.tint.g, put.tint.b = tint.r, tint.g, tint.b
    return tonumber(pdk.cg_model_put(cg, addr, put))
end

return M
