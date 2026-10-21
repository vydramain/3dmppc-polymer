-- Route math of example-cpp-route.cpp in float32: every result is rounded to float,
-- in the operation order of the C++, so the frames match it bit for bit.
-- A module has no state of its own; it holds only constants and pure functions.
local M = {}

-- Rounds x to float32 through a one-cell buffer. The buffer is made on first use,
-- not at load: loading a module and the chunk body must stay free of pdk calls.
local f32 = nil
local function f(x)
    if f32 == nil then
        f32 = pdk.new("float[1]")
    end
    f32[0] = x
    return f32[0]
end
M.f = f

-- A world matrix is row-major 4x4: 16 numbers.
M.MATRIX_LEN = 16

-- One period of the camera loop: 4320 frames = 72 s at 1/60 s per frame.
M.PERIOD_FRAMES = 4320

-- Below this length an interpolated direction has no heading; the camera looks down +z.
local MIN_LENGTH = 1e-6
local FALLBACK_DIR = { x = 0, y = 0, z = 1 }

function M.unit(v)
    local length = f(math.sqrt(f(f(f(v.x * v.x) + f(v.y * v.y)) + f(v.z * v.z))))
    if length < f(MIN_LENGTH) then
        return FALLBACK_DIR
    end
    return { x = f(v.x / length), y = f(v.y / length), z = f(v.z / length) }
end

-- One component of the uniform Catmull-Rom segment from p1 to p2, t in [0, 1].
-- Same operation order as spline_component in example-cpp-route.cpp.
local function spline_component(p0, p1, p2, p3, t)
    local t2 = f(t * t)
    local t3 = f(t2 * t)
    local slope = f(-p0 + p2)
    local curve = f(f(f(f(2 * p0) - f(5 * p1)) + f(4 * p2)) - p3)
    local cubic = f(f(f(-p0 + f(3 * p1)) - f(3 * p2)) + p3)
    local sum = f(f(f(2 * p1) + f(slope * t)) + f(curve * t2))
    sum = f(sum + f(cubic * t3))
    return f(0.5 * sum)
end

local function spline(a, b, c, d, t)
    return {
        x = spline_component(a.x, b.x, c.x, d.x, t),
        y = spline_component(a.y, b.y, c.y, d.y, t),
        z = spline_component(a.z, b.z, c.z, d.z, t),
    }
end

-- Eye and target of frame `frame` (taken modulo the period). keys is non-empty and sorted by index.
function M.camera_of(keys, frame)
    local count = #keys
    -- The integer split keeps the position exact per frame: no float sum over frames.
    local scaled = (frame % M.PERIOD_FRAMES) * count
    local seg = math.floor(scaled / M.PERIOD_FRAMES)
    local t = f((scaled % M.PERIOD_FRAMES) / M.PERIOD_FRAMES)
    -- The route is closed: the keys wrap around, so the last segment ends on key 1.
    local p0 = keys[(seg + count - 1) % count + 1]
    local p1 = keys[seg + 1]
    local p2 = keys[(seg + 1) % count + 1]
    local p3 = keys[(seg + 2) % count + 1]
    local eye = spline(p0.eye, p1.eye, p2.eye, p3.eye, t)
    local dir = M.unit(spline(p0.dir, p1.dir, p2.dir, p3.dir, t))
    return eye, { x = f(eye.x + dir.x), y = f(eye.y + dir.y), z = f(eye.z + dir.z) }
end

return M
