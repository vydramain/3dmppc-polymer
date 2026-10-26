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
-- Same values as the defaults of pdkkit's lens component (pdk/kit/include/pdkkit/ecs/rv_ecs_component.hpp), in
-- Q20.12. The up is not a lens default: the kit takes it from the lens entity's y column; this disc passes +y.
-- The field is in degrees. The near plane is 0.1 m = 409.6 raw, rounded half up to 410; the far plane
-- is a margin past the scene's extent.
local FX_ONE = 4096
local FOV_Y = 60 * FX_ONE
local NEAR_PLANE = 410
local FAR_PLANE = 100 * FX_ONE
local UP = { x = 0, y = FX_ONE, z = 0 }
-- rv_err.h codes: a refused scene answers RV_ERR_INVAL.
local RV_OK = 0
local RV_ERR_INVAL = -1
local BACKGROUND = { 20, 24, 40 }
-- The scene object that carries the factory board lettering, and the prefix of the street lamps.
local BOARD_NAME = "board"
local LAMP_PREFIX = "lamp_"

-- The state keeps its own numbers: Reload Module rewrites the layout's tables in place, and a
-- frame must not change with a reload. Copies one world: the layout's 1-based r (9) and t (3).
local function copy_world(world)
    local mathx = require("example-lua-bare-math")
    local out = { r = {}, t = {} }
    for k = 1, mathx.ROTATION_LEN do
        out.r[k] = world.r[k]
    end
    for k = 1, mathx.TRANSLATION_LEN do
        out.t[k] = world.t[k]
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

-- The eye is the translation t; the view direction is the z column of r, as pdkkit's camera step takes it
-- (ecs_system_camera, pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp). Answers RV_OK, or RV_ERR_INVAL when the
-- direction leaves Q20.12.
local function add_key(keys, entry, mathx)
    local digits = entry.name:match("^cam_(%d%d)$")
    if digits == nil then
        return RV_OK -- a camera outside the route is not part of the loop
    end
    local w = entry.world
    local dir = mathx.unit({ x = w.r[3], y = w.r[6], z = w.r[9] })
    if dir == nil then
        print("example-lua-bare: scene refused: camera '" .. entry.name .. "' direction leaves Q20.12")
        return RV_ERR_INVAL
    end
    keys[#keys + 1] = {
        index = tonumber(digits),
        eye = { x = w.t[1], y = w.t[2], z = w.t[3] },
        dir = dir,
    }
    return RV_OK
end

-- Fills `scene` from the layout's entries, in scene order. Answers RV_OK, or the code of a refused key.
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
            local code = add_key(scene.keys, entry, mathx)
            if code ~= RV_OK then
                return code
            end
        end
    end
    return RV_OK
end

-- Copies the meshes, the cam_NN keys (in NN order), the lamps and the board out of the layout.
-- With no keys there is no loop: the lists come back empty with the reason, so the disc runs without the scene.
local function scene_read(mathx)
    local scene = { meshes = {}, keys = {}, lamps = {} }
    local code = read_objects(scene, mathx)
    if code ~= RV_OK then
        return { meshes = {}, keys = {}, lamps = {} }, string.format("example-lua-bare-layout refused: rv_err %d", code)
    end
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
    if eye == nil then
        log_error_once("camera", target) -- on failure the second answer is the code
        return
    end
    local screen_height = tonumber(pdk.cv_screen_height(cv))
    local camera, make_code = pdk.gte_camera(eye, target, UP, FOV_Y, screen_height, NEAR_PLANE, FAR_PLANE)
    if camera == nil then
        log_error_once("camera", make_code)
        return
    end
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
