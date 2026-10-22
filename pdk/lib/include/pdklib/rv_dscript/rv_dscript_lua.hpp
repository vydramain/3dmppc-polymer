#pragma once

// The lua half of pdklib's disc-author toolbox. RV_MPPC_DISC_LUA_DEF
// (rv_dscript.hpp, beside this file) is the C++ half a disc's own .cpp
// writes to forward its hooks into a Lua chunk; this is what the SCRIPT
// SIDE of that same disc gets for free once the chunk is running. Both
// halves answer the one question pdklib exists to answer for a disc
// author - what must a primitive be filled with before the console will
// accept it, and how does a resource's name become an address - and never
// the question of what to draw with either: geometry, colour, position,
// depth and the decision to draw at all stay the game's.
//
// The console (src/rv_pconsole/cl/rv_pccl_luajit.cpp) RAISES this source
// the same way it raises its own RV_PCCL_PDK_BOOTSTRAP_SRC - one
// luaL_loadbuffer, one protected pcall, run right after that first chunk
// has finished building the `pdk` table these helpers extend - but the
// console does not AUTHOR it. Every helper below belongs to pdklib, the
// same way RV_MPPC_DISC_LUA_DEF's macro belongs to pdklib even though the
// console is what runs the disc it defines; the console only ever lends
// the Lua machine that already exists for the disc's own entry chunk.
namespace rv_pdklib
{

// `pdk` arrives as this chunk's one vararg, already carrying cast/new, the
// lazy ffi.C accessors and the per-controller `pdk.cv(o)`-style helpers
// RV_PCCL_PDK_BOOTSTRAP_SRC installed a moment earlier - this chunk only
// ADDS to that table, it never rebuilds it.
constexpr const char *RV_PDKLIB_LUA_HELPERS_SRC = R"lua(
local pdk = ...

-- A disc's resources are asked for by kind + name, four separate calls
-- (address, palette address, width, height) - see rv_cd_resource_addr's own
-- contract comment in pdk/include/pdk/cd/rv_cd.h. The kind is not the game's
-- choice: today there is exactly one, RV_CD_RESOURCE_TEXTURE, so a script
-- naming it four times over is pure repetition, not a decision - which is
-- why this helper hardcodes it and takes only the organizer and a name.
-- Returns nil rather than a table of negative addresses when the name does
-- not resolve, because that is the shape a game's own `if` already knows how
-- to test, and because a missing asset is exactly the kind of content
-- problem rv_cd_resource_addr's RV_ERR_NOENT describes, not a coding error
-- worth raising as one.
function pdk.resource_resolve(o, name)
    local cd = pdk.cd(o)
    local addr_texture = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_TEXTURE, name))
    if addr_texture < 0 then
        return nil
    end
    return {
        addr_texture = addr_texture,
        addr_palette = tonumber(pdk.cd_resource_palette_addr(cd, pdk.CD_RESOURCE_TEXTURE, name)),
        width = tonumber(pdk.cd_resource_width(cd, pdk.CD_RESOURCE_TEXTURE, name)),
        height = tonumber(pdk.cd_resource_height(cd, pdk.CD_RESOURCE_TEXTURE, name)),
    }
end

-- Every field rv_cv_frame_put REQUIRES from a textured sprite before it will
-- accept the primitive at all: which union member it is (type), how it is
-- filled (fill_mode), how sampling behaves past its edges (mapping), and the
-- resolved texture's own address pair - none of those are choices a game
-- makes differently disc to disc, they are what "this is a sprite" MEANS to
-- the console, so they belong here rather than being retyped in every
-- script. Width/height default to the texture's own pixel size because
-- "drawn at native size" is what a disc wants until it asks for otherwise,
-- and defaulting it here still leaves the game free to overwrite it.
-- Position (x, y), depth and colour are left at ffi.new's zero-init default
-- ON PURPOSE: only the game knows where a sprite belongs on screen, whether
-- it sits in front of or behind something else, and what tint it wears, and
-- setting any of those here would be pdklib deciding game content instead of
-- console plumbing.
function pdk.primitive_sprite(resolved)
    local primitive = pdk.new("rv_primitive")
    primitive.type = pdk.PRIMITIVE_SPRITE

    local sprite = primitive.data.sprite
    sprite.fill_mode = pdk.PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE
    sprite.mapping = pdk.TEXWRAP_CLAMP
    sprite.addr_texture = resolved.addr_texture
    sprite.addr_palette = resolved.addr_palette
    sprite.width = resolved.width
    sprite.height = resolved.height
    return primitive
end

-- The flat-coloured counterpart of pdk.primitive_sprite above, for the same
-- reason: a polygon with no texture to sample still has to carry an
-- addr_texture/addr_palette and a mapping (the field is not optional, even
-- though a flat fill never reads it), and vertex_count is the one number
-- the console needs before it will look at the vertex array at all. Zeroed
-- rather than left at whatever ffi.new already zero-initialised them to, so
-- the zero is a stated fact of a flat fill rather than an accident of
-- allocation. The vertices themselves - their positions and their colours -
-- are exactly what this primitive exists to draw, so pdklib does not touch
-- them: that is the game's content, never its plumbing, and vertex_count is
-- taken as a parameter rather than inferred so this helper never has to
-- guess it from data it is not given.
function pdk.primitive_polygon(vertex_count)
    local primitive = pdk.new("rv_primitive")
    primitive.type = pdk.PRIMITIVE_POLYGON

    local polygon = primitive.data.polygon
    polygon.fill_mode = pdk.PRIMITIVE_FILL_MODE_FLAT_COLOURED
    polygon.mapping = pdk.TEXWRAP_CLAMP
    polygon.addr_texture = 0
    polygon.addr_palette = 0
    polygon.vertex_count = vertex_count
    return primitive
end

-- Integer math ported from rv_math/rv_fx.hpp, rv_gte/rv_gte_trig.hpp, rv_gte/rv_gte_matrix.hpp,
-- rv_gte/rv_gte_project.hpp and rv_gte/rv_gte_camera.hpp. Past 2^53 values are int64 cdata; LuaJIT's
-- int64 '/' truncates toward zero, so divisions run on magnitudes. A refusal is nil, pdk.ERR_INVAL.
-- pdk.gte_round_frac and pdk.fx_isqrt refuse |v| above 2^53 (Lua numbers; C++ takes int64/uint64).
-- pdk.gte_fits is rv_gte_fits<int32_t>.
local ERR_INVAL = pdk.ERR_INVAL

local FX_ONE = 4096                        -- 1.0 in Q20.12 (fx_one, gte_one)
local DIR_ONE = 16777216LL                 -- 1.0 in Q.24: camera directions (gte_dir_one)
local CROSS_ONE = 68719476736LL            -- 2^36: a Q.48 cross product rounded to 4.12
local SIDE_MIN = FX_ONE                    -- gte_camera_min_side: |up x forward| in Q.24
local INT32_MIN = -2147483648
local INT32_MAX = 2147483647
local TURN_STEPS = 4096                    -- angle steps per turn (gte_turn_steps)
local QUARTER_STEPS = 1024                 -- gte_quarter_steps
local DEGREES_PER_TURN = 360
local DEG_DEN = DEGREES_PER_TURN * FX_ONE  -- one turn in raw Q20.12 degrees
local FOV_MIN = 10 * FX_ONE                -- gte_camera_fov_min_degrees, raw
local FOV_MAX = 180 * FX_ONE              -- gte_camera_fov_max_degrees, raw
local INT_EXACT_MAX = 9007199254740992     -- 2^53: the largest integer a Lua number holds exactly

-- Quarter sine of rv_gte_trig.hpp, computed: round(4096 * sin(i * step)), i = 0..1024.
local STEP_RADIANS = (math.pi / 2) / QUARTER_STEPS -- one table step of a quarter turn
local SIN_QUARTER = {}
for i = 0, QUARTER_STEPS do
    SIN_QUARTER[i] = math.floor(FX_ONE * math.sin(i * STEP_RADIANS) + 0.5)
end

local function i64(v) return v * 1LL end
local function fits32(v) return v >= INT32_MIN and v <= INT32_MAX end
local function is_int32(v) return type(v) == "number" and v % 1 == 0 and fits32(v) end
local function is_vec3(v) return type(v) == "table" and is_int32(v.x) and is_int32(v.y) and is_int32(v.z) end

-- Rounded half up (toward +inf), as rv_gte_div_round in pdklib/rv_gte/rv_gte_project.hpp; den > 0.
local function div_round(num, den)
    num, den = i64(num), i64(den)
    local m = num < 0 and -num or num
    local q = m / den
    local r = m - q * den
    if num >= 0 then
        return (2 * r >= den) and q + 1 or q
    end
    if r == 0 then
        return -q
    end
    return (2 * (den - r) >= den) and -q or -q - 1
end

-- fx_round_raw: p / 4096, half away from zero.
local function round_raw(p)
    local mag = p < 0 and -i64(p) or i64(p)
    local whole = mag / FX_ONE
    local r = (mag - whole * FX_ONE >= FX_ONE / 2) and whole + 1 or whole
    return p < 0 and -r or r
end

-- rv_gte_round_frac: v / 4096 with the C++ tie rule, half away from zero for v < 0.
local function round_frac(v)
    local m = i64(v)
    local q = ((m < 0 and -m or m) + FX_ONE / 2) / FX_ONE
    return m < 0 and -q or q
end

-- fx_isqrt: floor of the integer square root of n, bit method; n is uint64 cdata.
local function isqrt(n)
    local x = n * 1ULL
    local bit = 4611686018427387904ULL
    local root = 0ULL
    while bit > x do
        bit = bit / 4
    end
    while bit ~= 0 do
        if x >= root + bit then
            x = x - (root + bit)
            root = root / 2 + bit
        else
            root = root / 2
        end
        bit = bit / 4
    end
    return root
end

-- Square of a magnitude in uint64 (fx_sq_u64, rv_gte_camera_sq).
local function sq(v)
    local m = i64(v)
    local u = (m < 0 and -m or m) * 1ULL
    return u * u
end

-- Floor of the root of x^2 + y^2 + z^2 (fx_vec3_length, rv_gte_camera_length).
local function root_of(x, y, z)
    return isqrt(sq(x) + sq(y) + sq(z))
end

local function vec3_pack(x, y, z)
    if not (fits32(x) and fits32(y) and fits32(z)) then
        return nil, ERR_INVAL
    end
    return { x = tonumber(x), y = tonumber(y), z = tonumber(z) }
end

local function vec3_length(v)
    local root = root_of(v.x, v.y, v.z)
    if root > INT32_MAX then
        return nil, ERR_INVAL
    end
    return tonumber(root)
end

-- rv_gte_sin of an index in [0, 4096), by quadrant symmetry.
local function sin_wrapped(index)
    local quadrant = math.floor(index / QUARTER_STEPS)
    local offset = index % QUARTER_STEPS
    local magnitude = SIN_QUARTER[offset]
    if quadrant % 2 == 1 then
        magnitude = SIN_QUARTER[QUARTER_STEPS - offset]
    end
    if quadrant < 2 then
        return magnitude
    end
    return -magnitude
end

local function sin_of(index)
    return sin_wrapped(index % TURN_STEPS)
end

local function cos_of(index)
    return sin_wrapped((index + QUARTER_STEPS) % TURN_STEPS)
end

-- rv_gte_angle_index: raw Q20.12 degrees to a step in [0, 4096), half away from zero.
local function angle_index(degrees)
    local num = i64(degrees) * FX_ONE
    local mag = num < 0 and -num or num
    local turn = ((2 * mag + DEG_DEN) / (2 * DEG_DEN)) % TURN_STEPS
    if num < 0 and turn ~= 0 then
        turn = TURN_STEPS - turn
    end
    return tonumber(turn)
end

-- rv_gte_camera_half_sincos: sin and cos of a half field (raw, >= 0), scaled by 4096 * 4096,
-- linear between the two neighbouring steps.
local function half_sincos(raw)
    local big = i64(raw)
    local index = tonumber(big / DEGREES_PER_TURN)
    local frac = (big % DEGREES_PER_TURN) * FX_ONE / DEGREES_PER_TURN
    local lo = FX_ONE - frac
    local sin_num = sin_of(index) * lo + sin_of(index + 1) * frac
    local cos_num = cos_of(index) * lo + cos_of(index + 1) * frac
    return sin_num, cos_num
end

-- rv_gte_camera_focal: h = (height / 2) / tan(fov / 2), rounded half up.
local function camera_focal(fov, height)
    if fov < FOV_MIN or fov >= FOV_MAX then
        return nil, ERR_INVAL
    end
    local sin_num, cos_num = half_sincos(math.floor(fov / 2))
    local value = div_round(height * cos_num, 2 * sin_num)
    if value < 1 or not fits32(value) then
        return nil, ERR_INVAL
    end
    return tonumber(value)
end

local function cross(a, b)
    return {
        x = i64(a.y) * b.z - i64(a.z) * b.y,
        y = i64(a.z) * b.x - i64(a.x) * b.z,
        z = i64(a.x) * b.y - i64(a.y) * b.x,
    }
end

local function round_each(v, den)
    return { x = div_round(v.x, den), y = div_round(v.y, den), z = div_round(v.z, den) }
end

-- rv_gte_camera_dir: v as a Q.24 unit vector.
local function camera_dir(v)
    local len, rc = vec3_length(v)
    if not len then
        return nil, rc
    end
    if len == 0 then
        return nil, ERR_INVAL
    end
    return {
        x = div_round(i64(v.x) * DIR_ONE, len),
        y = div_round(i64(v.y) * DIR_ONE, len),
        z = div_round(i64(v.z) * DIR_ONE, len),
    }
end

-- rv_gte_camera_rows: right, up and forward rows of the left-handed look-at, 4.12.
local function camera_rows(eye, target, up)
    local aim, rc = vec3_pack(target.x - eye.x, target.y - eye.y, target.z - eye.z)
    if not aim then
        return nil, rc
    end
    local forward
    forward, rc = camera_dir(aim)
    if not forward then
        return nil, rc
    end
    local up_dir
    up_dir, rc = camera_dir(up)
    if not up_dir then
        return nil, rc
    end
    local side = round_each(cross(up_dir, forward), DIR_ONE)
    local side_len = tonumber(root_of(side.x, side.y, side.z))
    if side_len < SIDE_MIN then
        return nil, ERR_INVAL
    end
    local right = {
        x = div_round(side.x * FX_ONE, side_len),
        y = div_round(side.y * FX_ONE, side_len),
        z = div_round(side.z * FX_ONE, side_len),
    }
    local right_q24 = { x = right.x * FX_ONE, y = right.y * FX_ONE, z = right.z * FX_ONE }
    local upward = round_each(cross(forward, right_q24), CROSS_ONE)
    return { right, upward, round_each(forward, FX_ONE) }
end

-- rv_gte_camera_view: rows as r[0..8] and t = -row . eye, so the eye maps to 0.
local function camera_view(eye, target, up)
    local rows, rc = camera_rows(eye, target, up)
    if not rows then
        return nil, rc
    end
    local view = { r = {}, t = {} }
    for i = 1, 3 do
        local row = rows[i]
        view.r[(i - 1) * 3] = tonumber(row.x)
        view.r[(i - 1) * 3 + 1] = tonumber(row.y)
        view.r[(i - 1) * 3 + 2] = tonumber(row.z)
        local t = -round_frac(row.x * eye.x + row.y * eye.y + row.z * eye.z)
        if not fits32(t) then
            return nil, ERR_INVAL
        end
        view.t[i - 1] = tonumber(t)
    end
    return view
end

function pdk.fx_mul(a, b)
    if not (is_int32(a) and is_int32(b)) then
        return nil, ERR_INVAL
    end
    local r = round_raw(i64(a) * b)
    if not fits32(r) then
        return nil, ERR_INVAL
    end
    return tonumber(r)
end

function pdk.fx_div(a, b)
    if not (is_int32(a) and is_int32(b)) or b == 0 then
        return nil, ERR_INVAL
    end
    local num = i64(a) * FX_ONE
    local den = i64(b)
    local n = num < 0 and -num or num
    local d = den < 0 and -den or den
    local mag = (2 * n + d) / (2 * d)
    if (num < 0) ~= (den < 0) then
        mag = -mag
    end
    if not fits32(mag) then
        return nil, ERR_INVAL
    end
    return tonumber(mag)
end

function pdk.fx_sqrt(a)
    if not is_int32(a) or a < 0 then
        return nil, ERR_INVAL
    end
    return tonumber(isqrt(i64(a) * FX_ONE))
end

function pdk.fx_from_raw(q)
    if not is_int32(q) then
        return nil, ERR_INVAL
    end
    return q
end

function pdk.fx_isqrt(n)
    if type(n) ~= "number" or n < 0 or n % 1 ~= 0 or n > INT_EXACT_MAX then
        return nil, ERR_INVAL
    end
    return tonumber(isqrt(i64(n)))
end

function pdk.fx_vec3_add(a, b)
    if not (is_vec3(a) and is_vec3(b)) then
        return nil, ERR_INVAL
    end
    return vec3_pack(a.x + b.x, a.y + b.y, a.z + b.z)
end

function pdk.fx_vec3_sub(a, b)
    if not (is_vec3(a) and is_vec3(b)) then
        return nil, ERR_INVAL
    end
    return vec3_pack(a.x - b.x, a.y - b.y, a.z - b.z)
end

function pdk.fx_vec3_scale(v, s)
    if not (is_vec3(v) and is_int32(s)) then
        return nil, ERR_INVAL
    end
    return vec3_pack(round_raw(i64(v.x) * s), round_raw(i64(v.y) * s), round_raw(i64(v.z) * s))
end

function pdk.fx_vec3_length(v)
    if not is_vec3(v) then
        return nil, ERR_INVAL
    end
    return vec3_length(v)
end

function pdk.gte_round_frac(v)
    if type(v) ~= "number" or v % 1 ~= 0 or v > INT_EXACT_MAX or v < -INT_EXACT_MAX then
        return nil, ERR_INVAL
    end
    return tonumber(round_frac(v))
end

function pdk.gte_fits(v)
    return is_int32(v)
end

function pdk.gte_sin(index)
    if not is_int32(index) then
        return nil, ERR_INVAL
    end
    return sin_of(index)
end

function pdk.gte_cos(index)
    if not is_int32(index) then
        return nil, ERR_INVAL
    end
    return cos_of(index)
end

function pdk.gte_angle_index(degrees_fx)
    if not is_int32(degrees_fx) then
        return nil, ERR_INVAL
    end
    return angle_index(degrees_fx)
end

-- rv_gte_camera_make: a new rv_cg_camera from a left-handed look-at, the vertical fov in raw
-- degrees, the screen height in pixels and the clip planes in Q20.12.
function pdk.gte_camera(eye, target, up, fov_degrees_fx, screen_height, near_fx, far_fx)
    if not (is_vec3(eye) and is_vec3(target) and is_vec3(up)) then
        return nil, ERR_INVAL
    end
    if not (is_int32(fov_degrees_fx) and is_int32(near_fx) and is_int32(far_fx)) then
        return nil, ERR_INVAL
    end
    if not is_int32(screen_height) or screen_height <= 0 then
        return nil, ERR_INVAL
    end
    if near_fx <= 0 or far_fx <= near_fx then
        return nil, ERR_INVAL
    end
    local h, rc = camera_focal(fov_degrees_fx, screen_height)
    if not h then
        return nil, rc
    end
    local view
    view, rc = camera_view(eye, target, up)
    if not view then
        return nil, rc
    end
    local cam = pdk.new("rv_cg_camera")
    for i = 0, 8 do
        cam.view.r[i] = view.r[i]
    end
    for i = 0, 2 do
        cam.view.t[i] = view.t[i]
    end
    cam.h = h
    cam.near_plane = near_fx
    cam.far_plane = far_fx
    return cam
end
)lua";

} // namespace rv_pdklib
