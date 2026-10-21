-- example-lua-bare's entry chunk: the demo world of example-cpp-pdkkit in Lua - the scene's meshes,
-- the camera route, the street lamps and the factory board. The disc's own hooks
-- (src/example-lua-bare.cpp) forward one-to-one into the same-named functions here.
-- Nothing touches _G: the chunk returns a module table, and the disc calls into it by name
-- through rv_cl_script_call. The route math, the draw put, the lamps and the board are
-- modules (example-lua-bare-*.lua), loaded with require inside the hooks; a module holds no state.
local M = {}

-- `state` is not declared here: the console places its ONE persistent table
-- into this chunk's environment under this name, before any hook runs - at
-- boot and after every successful code reload. It is the only thing that
-- survives a reload: a chunk local or a field of M dies with the code.
-- Keep only numbers, strings, booleans and tables in it. A function or a
-- coroutine would keep the OLD bytecode alive after a reload; cdata is
-- userdata, which the console refuses. Keep plain copies of the layout's numbers
-- (see copy_world), never its own tables. The console learns the shape of `state` right after
-- disc_initialize and holds every later reload candidate to it.
--
-- The body stays PURE (no pdk call, no require, no write to `state`): it also runs to
-- validate a reload candidate before the console knows whether it will pass.
print("Hello from example lua!")

-- The object kinds of the layout (example-lua-bare-layout.lua).
local KIND_MESH = "mesh"
local KIND_CAMERA = "camera"
-- Same values as example-cpp-pdkkit (example-cpp-pdkkit-route.hpp, example-cpp-pdkkit.cpp).
-- Lens and clip planes; the far plane is a margin past the scene's extent.
-- As RV_ROUTE_FOV_Y in example-cpp-pdkkit-route.cpp: 60 degrees, converted to radians in float.
local FOV_DEGREES = 60
local DEGREES_PER_HALF_TURN = 180
local NEAR_PLANE = 0.1
local FAR_PLANE = 100.0
local UP = { x = 0, y = 1, z = 0 }
local BACKGROUND = { 20, 24, 40 }
-- The scene object that carries the factory board lettering, and the prefix of the street lamps.
local BOARD_NAME = "board"
local LAMP_PREFIX = "lamp_"

-- The state keeps its own numbers: Reload Module rewrites the layout's tables in place, and a
-- frame must not change with a reload. Copies one 1-based world of 16 numbers.
local function copy_world(world)
    local out = {}
    for k = 1, require("example-lua-bare-math").MATRIX_LEN do
        out[k] = world[k]
    end
    return out
end

local function copy_tint(tint)
    return { r = tint.r, g = tint.g, b = tint.b }
end

local function add_mesh(meshes, entry)
    if entry.model == "" then
        print("warning: example-lua-bare: mesh '" .. entry.name .. "' has no model, not drawn")
        return
    end
    meshes[#meshes + 1] = { model = entry.model, world = copy_world(entry.world), tint = copy_tint(entry.tint) }
end

-- The camera's eye is world * (0,0,0) and its view direction world * (0,0,1) minus the eye.
-- The layout's world is 1-based: the translation is column 4, the z axis column 3.
local function add_key(keys, entry, mathx)
    local digits = entry.name:match("^cam_(%d%d)$")
    if digits == nil then
        return -- a camera outside the route is not part of the loop
    end
    local w = entry.world
    keys[#keys + 1] = {
        index = tonumber(digits),
        eye = { x = w[4], y = w[8], z = w[12] },
        dir = mathx.unit({ x = w[3], y = w[7], z = w[11] }),
    }
end

-- Fills `scene` from the layout's entries, in scene order.
local function read_objects(scene, mathx)
    for _, entry in ipairs(require("example-lua-bare-layout")) do
        if entry.name == BOARD_NAME then
            scene.board = copy_world(entry.world)
        end
        if entry.kind == KIND_MESH then
            add_mesh(scene.meshes, entry)
            if #entry.name > #LAMP_PREFIX and entry.name:sub(1, #LAMP_PREFIX) == LAMP_PREFIX then
                scene.lamps[#scene.lamps + 1] = {
                    name = entry.name,
                    world = copy_world(entry.world),
                    on_frame = 0,
                    off_frame = 0,
                }
            end
        elseif entry.kind == KIND_CAMERA then
            add_key(scene.keys, entry, mathx)
        end
    end
end

-- Copies the meshes, the cam_NN keys (in NN order), the lamps and the board out of the layout.
-- With no keys there is no loop: the lists come back empty with the reason, so the disc runs without the scene.
local function scene_read(mathx)
    local scene = { meshes = {}, keys = {}, lamps = {} }
    read_objects(scene, mathx)
    if #scene.keys == 0 then
        return { meshes = {}, keys = {}, lamps = {} }, "example-lua-bare-layout not used: no cam_NN keys"
    end
    table.sort(scene.keys, function(a, b)
        return a.index < b.index
    end)
    return scene
end

-- Logs the first failure of each step only: the same failure would repeat every frame.
-- state.logged_errors is keyed by step (the camera, the lamps, the board, or a model name).
local function log_error_once(step, code, failure)
    if state.logged_errors[step] then
        return
    end
    state.logged_errors[step] = true
    print(string.format("example-lua-bare: %s %s: rv_err %d", step, failure or "not drawn", code))
end

-- The disc's C++ side (src/example-lua-bare.cpp) calls this with the organizer handle `o_`;
-- pdk.cv(o_), pdk.cd(o_) and pdk.cg(o_) resolve it into the controller a call needs.
function M.disc_initialize(o_)
    state.frame_count = 0
    state.logged_errors = {}
    -- The music's tables (example-lua-bare-sound.lua): ready after its init, frame -1 before the first frame.
    state.sound = { ready = false, frame = -1, area = 0, bar = 0, slots = {}, error_asset = "" }
    local mathx = require("example-lua-bare-math")
    local scene, reason = scene_read(mathx)
    state.meshes = scene.meshes
    state.keys = scene.keys
    state.lamps = scene.lamps
    state.board_world = scene.board or {}
    state.board_found = scene.board ~= nil
    if reason ~= nil then
        print("example-lua-bare: " .. reason)
    end
    if #state.keys > 0 then
        require("example-lua-bare-lamps").schedule(state.keys, state.lamps)
        local sound_code = require("example-lua-bare-sound").init(pdk.ca(o_), pdk.cd(o_), state.keys, state)
        if sound_code < 0 then
            print(string.format("example-lua-bare: sound off: %s rv_err %d", state.sound.error_asset, sound_code))
        end
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

    local mathx = require("example-lua-bare-math")
    local at = state.frame_count % mathx.PERIOD_FRAMES
    -- The sound first, as in example-cpp-pdkkit's frame_render: the music and the clack, then the picture.
    local sound_code = require("example-lua-bare-sound").update(pdk.ca(o_), cd, state.keys, at, state.board_found, state)
    if sound_code < 0 then
        log_error_once("sound " .. state.sound.error_asset, sound_code, "not played")
    end
    local eye, target = mathx.camera_of(state.keys, state.frame_count)
    local camera = pdk.new("rv_cg_camera")
    camera.eye.x, camera.eye.y, camera.eye.z = eye.x, eye.y, eye.z
    camera.target.x, camera.target.y, camera.target.z = target.x, target.y, target.z
    camera.up.x, camera.up.y, camera.up.z = UP.x, UP.y, UP.z
    -- FOV_DEGREES * pi_v<float> / DEGREES_PER_HALF_TURN, each step rounded to float as in C++
    camera.fov_y = mathx.f(mathx.f(FOV_DEGREES * mathx.f(math.pi)) / DEGREES_PER_HALF_TURN)
    camera.near_plane = NEAR_PLANE
    camera.far_plane = FAR_PLANE
    local camera_code = tonumber(pdk.cg_frame_camera(cg, camera))
    if camera_code < 0 then
        log_error_once("camera", camera_code)
        return
    end

    -- The order of example-cpp-pdkkit's draw_world: meshes, then lamps, then the board.
    local draw = require("example-lua-bare-draw")
    for _, mesh in ipairs(state.meshes) do
        local code = draw.put(cg, cd, mesh.model, mesh.world, mesh.tint)
        if code < 0 then
            log_error_once(mesh.model, code)
        end
    end
    local lamps_code = require("example-lua-bare-lamps").draw(cg, cd, state.lamps, at)
    if lamps_code < 0 then
        log_error_once("lamps", lamps_code)
    end
    if state.board_found then
        local board_code = require("example-lua-bare-board").draw(cg, cd, state.board_world, at)
        if board_code < 0 then
            log_error_once("board", board_code)
        end
    end
end

function M.disc_shutdown(o_) end

return M
