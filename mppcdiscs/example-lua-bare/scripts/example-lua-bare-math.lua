-- Route math of example-cpp-pdkkit-route.cpp in Q20.12, in plain Lua integers: the same operations in the
-- same order, so the frames match the C++ bit for bit. Values are integers held in doubles; every product
-- and sum stays below 2^53 because the keys are bounded (MAX_COORD). The only state is the weak set of keys
-- already checked, so a check runs once per keys table.
local M = {}

-- Codes of rv_err.h: a route without keys is RV_ERR_NOENT, a value outside the bounds is RV_ERR_INVAL.
local RV_ERR_INVAL = -1
local RV_ERR_NOENT = -4
M.ERR_INVAL = RV_ERR_INVAL

local floor = math.floor
local abs = math.abs
local sqrt = math.sqrt

-- Q20.12 of 1.0 (rv_pdklib::fx_one).
local ONE = 4096
-- Half of one: the rounding step of fx_round_raw, which rounds half away from zero.
local HALF_ONE = 2048

-- A world is { r = 9 numbers, t = 3 numbers }: r is the 4.12 rotation rows, t the Q20.12 translation.
M.ROTATION_LEN = 9
M.TRANSLATION_LEN = 3

-- One period of the camera loop: 4320 frames = 72 s at 1/60 s per frame.
M.PERIOD_FRAMES = 4320
local PERIOD = M.PERIOD_FRAMES

-- Largest |component| of a key's eye or dir, raw: 2^20 = 256 m. A bound of the proof, not a scene limit.
local MAX_COORD = 1048576
-- A spline value is at most 12 key bounds: the absolute weights per key are 4, 10, 8 and 2 (24 in all),
-- and the Catmull-Rom point halves their sum.
local SPLINE_GAIN = 12
local MAX_SPLINE = SPLINE_GAIN * MAX_COORD

-- Raw steps are 1/4096 and the length is rounded down, so only a zero vector is shorter than one step;
-- the camera then looks down +z.
local MIN_LENGTH = 1

-- p / ONE rounded half away from zero (fx_round_raw). p is an integer with |p| below 2^53.
local function round_raw(p)
    local mag = floor((abs(p) + HALF_ONE) / ONE)
    if p < 0 then
        return -mag
    end
    return mag
end

-- a / b in Q20.12, nearest, half away from zero (fx_div), b a nonzero integer. The quotient floors exactly:
-- its magnitude is about |a| * ONE, below 2^36, so the gap to the next integer is far above the double error.
local function div_raw(a, b)
    local n = abs(a) * ONE
    local d = abs(b)
    local mag = floor((2 * n + d) / (2 * d))
    if (a < 0) ~= (b < 0) then
        return -mag
    end
    return mag
end

-- Floor of the square root of an integer n below 2^53: the float root, corrected.
local function isqrt(n)
    local r = floor(sqrt(n))
    while r * r > n do
        r = r - 1
    end
    while (r + 1) * (r + 1) <= n do
        r = r + 1
    end
    return r
end

-- True when c is an integer with |c| <= bound.
local function within(c, bound)
    return type(c) == "number" and c % 1 == 0 and c >= -bound and c <= bound
end

-- Unit vector of (x, y, z) in Q20.12, or the +z fallback below MIN_LENGTH. The caller keeps
-- the components within MAX_SPLINE, so the squares of the length stay below 2^53.
local function unit_of(x, y, z)
    local length = isqrt(x * x + y * y + z * z)
    if length < MIN_LENGTH then
        return 0, 0, ONE
    end
    return div_raw(x, length), div_raw(y, length), div_raw(z, length)
end

-- Unit vector of v in Q20.12 as { x, y, z }. Answers nil and RV_ERR_INVAL when a component is outside MAX_SPLINE.
function M.unit(v)
    if not (within(v.x, MAX_SPLINE) and within(v.y, MAX_SPLINE) and within(v.z, MAX_SPLINE)) then
        return nil, RV_ERR_INVAL
    end
    local x, y, z = unit_of(v.x, v.y, v.z)
    return { x = x, y = y, z = z }
end

-- True when v = { x, y, z } is a point the route and the lamps can use: each component within MAX_COORD.
function M.in_range(v)
    return within(v.x, MAX_COORD) and within(v.y, MAX_COORD) and within(v.z, MAX_COORD)
end

-- One axis of the Catmull-Rom point between p2 and p3 at t, t2 = t^2 and t3 = t^3 (route.cpp spline). The
-- weights are whole multiples of ONE, so the weighted terms are exact integers; each scaled term is rounded
-- once, as fx_vec3_scale does. Keys p1..p4 are the four keys of the segment, p1 before it.
local function spline_axis(p1, p2, p3, p4, t, t2, t3)
    local base = 2 * p2
    local slope = p3 - p1
    local curve = 2 * p1 - 5 * p2 + 4 * p3 - p4
    local cubic = -p1 + 3 * p2 - 3 * p3 + p4
    local head = base + round_raw(slope * t)
    local tail = round_raw(curve * t2) + round_raw(cubic * t3)
    return round_raw((head + tail) * HALF_ONE)
end

-- The point of `field` ("eye" or "dir") at `frame` (taken modulo the period): x, y, z of the spline (route_spline).
local function route_point(keys, frame, field)
    local count = #keys
    local scaled = (frame % PERIOD) * count
    local seg = floor(scaled / PERIOD)
    local rem = scaled % PERIOD
    -- t = rem / PERIOD in Q20.12 (fx_div); rem is below PERIOD, so t is in [0, ONE].
    local t = floor((2 * rem * ONE + PERIOD) / (2 * PERIOD))
    local t2 = round_raw(t * t)
    local t3 = round_raw(t2 * t)
    -- The route is closed: the keys wrap around, so the last segment ends on key 1.
    local k1 = keys[(seg + count - 1) % count + 1][field]
    local k2 = keys[(seg + count) % count + 1][field]
    local k3 = keys[(seg + count + 1) % count + 1][field]
    local k4 = keys[(seg + count + 2) % count + 1][field]
    return spline_axis(k1.x, k2.x, k3.x, k4.x, t, t2, t3),
        spline_axis(k1.y, k2.y, k3.y, k4.y, t, t2, t3),
        spline_axis(k1.z, k2.z, k3.z, k4.z, t, t2, t3)
end

-- Keys that already passed the check, weak so a dropped keys table leaves no entry.
local checked = setmetatable({}, { __mode = "k" })

-- Answers nil when keys is a usable route, else RV_ERR_NOENT without keys or RV_ERR_INVAL when an eye or dir
-- of a key is not an integer within MAX_COORD. The check runs once per keys table.
local function keys_code(keys)
    if checked[keys] then
        return nil
    end
    if #keys == 0 then
        return RV_ERR_NOENT
    end
    for _, key in ipairs(keys) do
        if not (M.in_range(key.eye) and M.in_range(key.dir)) then
            return RV_ERR_INVAL
        end
    end
    checked[keys] = true
    return nil
end

-- Eye and target of frame `frame` (taken modulo the period). keys is sorted by index.
-- Answers eye, target, or nil and a rv_err code.
function M.camera_of(keys, frame)
    local code = keys_code(keys)
    if code ~= nil then
        return nil, code
    end
    local ex, ey, ez = route_point(keys, frame, "eye")
    local dx, dy, dz = route_point(keys, frame, "dir")
    local ux, uy, uz = unit_of(dx, dy, dz)
    return { x = ex, y = ey, z = ez }, { x = ex + ux, y = ey + uy, z = ez + uz }
end

-- The eye of every frame of the period, as flat arrays { x = {...}, y = {...}, z = {...} } with frame f at
-- index f + 1. Answers nil and a rv_err code as camera_of does.
function M.period_eyes(keys)
    local code = keys_code(keys)
    if code ~= nil then
        return nil, code
    end
    local xs, ys, zs = {}, {}, {}
    for frame = 0, PERIOD - 1 do
        xs[frame + 1], ys[frame + 1], zs[frame + 1] = route_point(keys, frame, "eye")
    end
    return { x = xs, y = ys, z = zs }
end

return M
