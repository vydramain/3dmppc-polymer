-- Street lamps (example-cpp-lamps.cpp). A lamp burns from the first frame of the outward
-- half in which the eye comes within 8 m of it, until the first such frame of the return half.
local mathx = require("example-lua-math")
local draw = require("example-lua-draw")
local M = {}

local LIGHT_MODEL = "lamp_light.mppcmod"
-- Squared reach, metres: 8 m. Exact in float, so no rounding is needed.
local REACH_SQUARED = 8 * 8
-- The outward half is frames [0, P/2); the turn is in the flat at P/2 (key cam_10, frame 2160) and belongs to the return half.
local OUTWARD_END = math.floor(mathx.PERIOD_FRAMES / 2)

-- First frame in [from, to) whose eye is within the reach of at (1-based world column), -1 when none.
local function first_near(keys, at, from, to)
    for frame = from, to - 1 do
        local eye = mathx.camera_of(keys, frame)
        local dx = mathx.f(eye.x - at.x)
        local dy = mathx.f(eye.y - at.y)
        local dz = mathx.f(eye.z - at.z)
        if mathx.f(mathx.f(mathx.f(dx * dx) + mathx.f(dy * dy)) + mathx.f(dz * dz)) < REACH_SQUARED then
            return frame
        end
    end
    return -1
end

-- Sets on_frame and off_frame of each lamp. A lamp the route does not pass in both halves
-- stays dark (0, 0) and gets one warning.
function M.schedule(keys, lamps)
    for _, lamp in ipairs(lamps) do
        -- The translation column of the lamp's world: its base on the street.
        local at = { x = lamp.world[4], y = lamp.world[8], z = lamp.world[12] }
        local on = first_near(keys, at, 0, OUTWARD_END)
        local off = first_near(keys, at, OUTWARD_END, mathx.PERIOD_FRAMES)
        if on < 0 or off < 0 then
            lamp.on_frame, lamp.off_frame = 0, 0
            print("warning: example-lua: " .. lamp.name .. " is not passed by the route in both halves: it stays dark")
        else
            lamp.on_frame, lamp.off_frame = on, off
            print(string.format("example-lua: %s burns from frame %d to %d of the period", lamp.name, on, off))
        end
    end
end

-- Puts the light of each lamp that burns at frame (within the period). Returns 0 or the first error.
function M.draw(cg, cd, lamps, frame)
    for _, lamp in ipairs(lamps) do
        if frame >= lamp.on_frame and frame < lamp.off_frame then
            local code = draw.put(cg, cd, LIGHT_MODEL, lamp.world, draw.WHITE)
            if code < 0 then
                return code
            end
        end
    end
    return 0
end

return M
