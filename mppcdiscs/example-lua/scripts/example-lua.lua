-- example-lua's entry chunk: the demo world and camera route of example-cpp, in Lua.
-- The disc's own hooks (src/example-lua.cpp) forward one-to-one into the
-- same-named functions here. Nothing touches _G: the chunk returns a module
-- table, and the disc calls into it by name through rv_cl_script_call.
local M = {}

-- `state` is not declared here: the console places its ONE persistent table
-- into this chunk's environment under this name, before any hook runs - at
-- boot and after every successful code reload. It is the only thing that
-- survives a reload: a chunk local or a field of M dies with the code.
-- Keep only numbers, strings, booleans and tables in it. A function or a
-- coroutine would keep the OLD bytecode alive after a reload; cdata is
-- userdata, which the console refuses. Copy plain values out of cdata first
-- (see scene_read). The console learns the shape of `state` right after
-- disc_initialize and holds every later reload candidate to it.
--
-- The body stays PURE (no pdk call, no write to `state`): it also runs to
-- validate a reload candidate before the console knows whether it will pass.
print("Hello from example lua!")

-- Same values as example-cpp (example-cpp-route.hpp, example-cpp.cpp).
local SCENE_NAME = "main.scene.toml"
-- One period of the camera loop: 1080 frames = 18 s at 1/60 s per frame.
local PERIOD_FRAMES = 1080
-- Lens and clip planes; the far plane is a margin past the scene's extent.
-- As RV_ROUTE_FOV_Y in example-cpp-route.cpp: 60 degrees, converted to radians in float.
local FOV_DEGREES = 60
local DEGREES_PER_HALF_TURN = 180
local NEAR_PLANE = 0.1
local FAR_PLANE = 100.0
local UP = { x = 0, y = 1, z = 0 }
-- Below this length an interpolated direction has no heading; the camera looks down +z.
local MIN_LENGTH = 1e-6
local FALLBACK_DIR = { x = 0, y = 0, z = 1 }
local BACKGROUND = { 20, 24, 40 }
-- The ordering-table window of the world, as example-cpp-draw.hpp.
local WORLD_DEPTH_MIN = 0
local WORLD_DEPTH_MAX = 959

-- The route math runs in float32 like example-cpp-route.cpp: f() rounds each
-- operation's result to float, so every frame matches the C++ bit for bit.
-- The buffer is made on first use, not in the chunk body, which must stay pure.
local f32 = nil
local function f(x)
    if f32 == nil then
        f32 = pdk.new("float[1]")
    end
    f32[0] = x
    return f32[0]
end

-- A NUL-terminated char array of the drive's scene record, as a Lua string.
local function cstring(chars)
    local out = {}
    for i = 0, pdk.CD_NAME_CAPACITY - 1 do
        local c = chars[i]
        if c == 0 then
            break
        end
        out[#out + 1] = string.char(c % 256)
    end
    return table.concat(out)
end

local function unit(v)
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
local function camera_of(keys, frame)
    local count = #keys
    -- The integer split keeps the position exact per frame: no float sum over frames.
    local scaled = (frame % PERIOD_FRAMES) * count
    local seg = math.floor(scaled / PERIOD_FRAMES)
    local t = f((scaled % PERIOD_FRAMES) / PERIOD_FRAMES)
    -- The route is closed: the keys wrap around, so the last segment ends on key 1.
    local p0 = keys[(seg + count - 1) % count + 1]
    local p1 = keys[seg + 1]
    local p2 = keys[(seg + 1) % count + 1]
    local p3 = keys[(seg + 2) % count + 1]
    local eye = spline(p0.eye, p1.eye, p2.eye, p3.eye, t)
    local dir = unit(spline(p0.dir, p1.dir, p2.dir, p3.dir, t))
    return eye, { x = f(eye.x + dir.x), y = f(eye.y + dir.y), z = f(eye.z + dir.z) }
end

local function add_mesh(meshes, object)
    local model = cstring(object.model)
    if model == "" then
        print("warning: example-lua: mesh '" .. cstring(object.name) .. "' has no model, not drawn")
        return
    end
    local world = {}
    for k = 0, 15 do
        world[k + 1] = object.world[k]
    end
    local tint = { r = object.tint.r, g = object.tint.g, b = object.tint.b }
    meshes[#meshes + 1] = { model = model, world = world, tint = tint }
end

-- The camera's eye is world * (0,0,0) and its view direction world * (0,0,1) minus the eye.
local function add_key(keys, object)
    local digits = cstring(object.name):match("^cam_(%d%d)$")
    if digits == nil then
        return -- a camera outside the route is not part of the loop
    end
    local w = object.world
    keys[#keys + 1] = {
        index = tonumber(digits),
        eye = { x = w[3], y = w[7], z = w[11] },
        dir = unit({ x = w[2], y = w[6], z = w[10] }),
    }
end

-- Copies the meshes and the cam_NN keys (in NN order) out of the scene. On failure
-- both lists come back empty with the reason, so the disc runs without the scene.
local function scene_read(cd)
    local meshes, keys = {}, {}
    local count = tonumber(pdk.cd_scene_object_count(cd, SCENE_NAME))
    if count < 0 then
        return {}, {}, string.format("%s not used: rv_err %d", SCENE_NAME, count)
    end
    local object = pdk.new("rv_cd_scene_object")
    for i = 0, count - 1 do
        local read = tonumber(pdk.cd_scene_object_read(cd, SCENE_NAME, i, object))
        if read < 0 then
            return {}, {}, string.format("%s not used: object %d rv_err %d", SCENE_NAME, i, read)
        end
        if object.kind == pdk.CD_SCENE_MESH then
            add_mesh(meshes, object)
        elseif object.kind == pdk.CD_SCENE_CAMERA then
            add_key(keys, object)
        end
    end
    if #keys == 0 then
        return {}, {}, SCENE_NAME .. " not used: no cam_NN keys"
    end
    table.sort(keys, function(a, b)
        return a.index < b.index
    end)
    return meshes, keys
end

-- Logs the first failure of each step only: the same failure would repeat every frame.
-- state.logged_errors is keyed by step (the camera, or a model name), so a second
-- model failing is still reported once.
local function log_error_once(step, code)
    if state.logged_errors[step] then
        return
    end
    state.logged_errors[step] = true
    print(string.format("example-lua: %s not drawn: rv_err %d", step, code))
end

-- The disc's C++ side (src/example-lua.cpp) calls this with the organizer handle `o_`;
-- pdk.cv(o_), pdk.cd(o_) and pdk.cg(o_) resolve it into the controller a call needs.
function M.disc_initialize(o_)
    state.frame_count = 0
    state.logged_errors = {}
    local meshes, keys, reason = scene_read(pdk.cd(o_))
    state.meshes = meshes
    state.keys = keys
    if reason ~= nil then
        print("example-lua: " .. reason)
    end
end

function M.frame_update(dt, o_)
    -- The demo's clock: frames updated while running, not while paused.
    if dt > 0 then
        state.frame_count = state.frame_count + 1
    end
    return false
end

function M.frame_render(o_)
    local cv = pdk.cv(o_)
    local cg = pdk.cg(o_)
    local cd = pdk.cd(o_)
    -- The flush is left to the disc's C++ side, right after this hook returns.
    pdk.cv_frame_configure(cv, 0, pdk.new("rv_color", BACKGROUND))
    if #state.keys == 0 then
        return -- disc_initialize has already said why
    end

    local eye, target = camera_of(state.keys, state.frame_count)
    local camera = pdk.new("rv_cg_camera")
    camera.eye.x, camera.eye.y, camera.eye.z = eye.x, eye.y, eye.z
    camera.target.x, camera.target.y, camera.target.z = target.x, target.y, target.z
    camera.up.x, camera.up.y, camera.up.z = UP.x, UP.y, UP.z
    -- FOV_DEGREES * pi_v<float> / DEGREES_PER_HALF_TURN, each step rounded to float as in C++
    camera.fov_y = f(f(FOV_DEGREES * f(math.pi)) / DEGREES_PER_HALF_TURN)
    camera.near_plane = NEAR_PLANE
    camera.far_plane = FAR_PLANE
    local camera_code = tonumber(pdk.cg_frame_camera(cg, camera))
    if camera_code < 0 then
        log_error_once("camera", camera_code)
        return
    end

    local put = pdk.new("rv_cg_put")
    put.depth_min = WORLD_DEPTH_MIN
    put.depth_max = WORLD_DEPTH_MAX
    put.cull = pdk.CG_CULL_BACK
    for _, mesh in ipairs(state.meshes) do
        local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_MODEL, mesh.model))
        local code = addr
        if addr >= 0 then
            for k = 0, 15 do
                put.world[k] = mesh.world[k + 1]
            end
            put.tint.r, put.tint.g, put.tint.b = mesh.tint.r, mesh.tint.g, mesh.tint.b
            code = tonumber(pdk.cg_model_put(cg, addr, put))
        end
        if code < 0 then
            log_error_once(mesh.model, code)
        end
    end
end

function M.disc_shutdown(o_) end

return M
