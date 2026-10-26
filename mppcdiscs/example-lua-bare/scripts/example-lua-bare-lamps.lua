-- Street lamps (example-cpp-pdkkit-lamps.cpp), in Q20.12. A lamp burns from the first frame of the outward
-- half in which the eye comes within 8 m of it, until the first such frame of the return half.
local mathx = require("example-lua-bare-math")
local draw = require("example-lua-bare-draw")
local M = {}

local LIGHT_MODEL = "lamp_light.mppcmod"
-- A lamp burns while the eye is nearer than this, metres: 8 m in Q20.12 is 8 * 4096 raw steps.
local REACH = 32768
-- REACH squared: the floor of the root of s is below REACH exactly when s is below REACH^2.
local REACH_SQ = REACH * REACH
-- The outward half is frames [0, P/2); the turn is in the flat at P/2 (key cam_10, frame 2160) and belongs to the return half.
local OUTWARD_END = math.floor(mathx.PERIOD_FRAMES / 2)

-- First frame in [from, to) whose eye is within REACH of at, -1 when none. eyes is period_eyes' table.
local function first_near(eyes, at, from, to)
    for frame = from, to - 1 do
        local dx = eyes.x[frame + 1] - at.x
        local dy = eyes.y[frame + 1] - at.y
        local dz = eyes.z[frame + 1] - at.z
        if dx * dx + dy * dy + dz * dz < REACH_SQ then
            return frame
        end
    end
    return -1
end

-- A lamp the route does not light: dark for the whole period, warned once.
local function stays_dark(lamp, reason)
    lamp.on_frame, lamp.off_frame = 0, 0
    print("warning: example-lua-bare: " .. lamp.name .. " " .. reason)
end

-- Sets on_frame and off_frame of each lamp. A lamp the route does not pass in both halves
-- stays dark (0, 0) and gets one warning.
function M.schedule(keys, lamps)
    local eyes, code = mathx.period_eyes(keys)
    for _, lamp in ipairs(lamps) do
        -- The translation of the lamp's world, Q20.12: its base on the street.
        local at = { x = lamp.world.t[1], y = lamp.world.t[2], z = lamp.world.t[3] }
        if eyes == nil or not mathx.in_range(at) then
            stays_dark(lamp, "is refused by the route, it stays dark: code " .. (code or mathx.ERR_INVAL))
        else
            local on = first_near(eyes, at, 0, OUTWARD_END)
            local off = first_near(eyes, at, OUTWARD_END, mathx.PERIOD_FRAMES)
            if on < 0 or off < 0 then
                stays_dark(lamp, "is not passed by the route in both halves: it stays dark")
            else
                lamp.on_frame, lamp.off_frame = on, off
                print(string.format("example-lua-bare: %s burns from frame %d to %d of the period", lamp.name, on, off))
            end
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
