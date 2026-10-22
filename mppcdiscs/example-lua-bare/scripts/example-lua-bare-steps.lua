-- The demo's footsteps (example-cpp-pdkkit-steps.cpp): one step per 0.95 m of horizontal eye path, on the floor of
-- the frame's area, A and B alternating. The distance is tabulated once over the period; a frame whose
-- distance crosses a multiple of the stride takes a step.
local mathx = require("example-lua-bare-math")
local M = {}

-- One step per this many metres of horizontal eye path: Solid Maid's SM_STEP_STRIDE (solid-maid/src/sm_common.hpp),
-- so the footfall keeps its pace.
local STRIDE = 0.95

-- Step voices: A takes even steps and B odd ones, as Solid Maid alternates its pair (sm_sound.cpp footstep).
local VOICES = { 32, 64 }
-- Steps cycle through the voices; the variant is the step's index modulo this.
local VARIANTS = #VOICES

-- Volume and envelope as Solid Maid plays a footstep: gain 1 and centre pan (sm_game.cpp, sm_sound.cpp play), so the
-- level is 22000 and the envelope is flat.
local VOLUME = 22000
local ENVELOPE = 0
-- Full level of the int16 sl, volume_l and volume_r fields.
local MAX_LEVEL = 32767

-- The two samples of each area's floor, indexed by area + 1: workshop concrete, street asphalt, flat linoleum.
local ASSETS = {
    { "sfx_step_concrete_a.pcm", "sfx_step_concrete_b.pcm" },
    { "sfx_step_asphalt_a.pcm", "sfx_step_asphalt_b.pcm" },
    { "sfx_step_lino_a.pcm", "sfx_step_lino_b.pcm" },
}

-- The distance table is a module local, not state: the console's state walk caps its nodes and refuses
-- the cdata of the state. A script reload drops it; init or the next update builds it again.
local distance = nil

-- Frame f is at index f + 1. Index 1 is frame 0, the cumulative distance is 0 there. Answers the table,
-- or nil and the rv_err code of the route.
local function build(keys)
    local eyes, code = mathx.period_eyes(keys)
    if eyes == nil then
        return nil, code
    end
    local period = mathx.PERIOD_FRAMES
    local table_of = {}
    local total = 0
    table_of[1] = 0
    for f = 1, period - 1 do
        local dx = eyes.x[f + 1] - eyes.x[f]
        local dz = eyes.z[f + 1] - eyes.z[f]
        total = total + math.sqrt(dx * dx + dz * dz)
        table_of[f + 1] = total
    end
    return table_of
end

-- Builds the distance table at the sound's init. Answers 0 or the route's code, which is also printed.
function M.init(keys)
    local table_of, code = build(keys)
    if table_of == nil then
        print("warning: example-lua-bare: step table refused: code " .. code)
        return code
    end
    distance = table_of
    return 0
end

-- The distance table, built if a reload dropped it. A refused route answers an empty table, which
-- sound.update refuses as not period-sized.
function M.distance(keys)
    if distance == nil then
        local table_of = build(keys)
        if table_of == nil then
            return {}
        end
        distance = table_of
    end
    return distance
end

-- Plays the step of frame `frame` when its distance crosses a stride, on the floor of `area`. Frame 0 is the
-- seam: no step crosses the wrap of the period. Returns 0 or the first error, with sound.error_asset
-- naming the asset.
function M.update(ca, cd, table_of, frame, area, sound)
    if frame == 0 then
        return 0
    end
    local step_before = math.floor(table_of[frame] / STRIDE)
    local step_now = math.floor(table_of[frame + 1] / STRIDE)
    if step_now <= step_before then
        return 0
    end
    local variant = step_now % VARIANTS
    local name = ASSETS[area + 1][variant + 1]
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, name))
    if addr < 0 then
        sound.error_asset = name
        return addr
    end
    local conf = pdk.new("rv_voice_conf")
    conf.voice = VOICES[variant + 1]
    conf.loop_type = pdk.LOOP_NONE
    conf.sample_address = addr
    conf.ar = ENVELOPE
    conf.dr = ENVELOPE
    conf.sr = ENVELOPE
    conf.rr = ENVELOPE
    conf.sl = MAX_LEVEL
    conf.volume = VOLUME
    conf.volume_l = MAX_LEVEL
    conf.volume_r = MAX_LEVEL
    -- As the clack: setup then play restarts a voice still playing, so no stop comes first.
    local setup_code = tonumber(pdk.ca_voice_setup(ca, conf))
    if setup_code < 0 then
        sound.error_asset = name
        return setup_code
    end
    local play_code = tonumber(pdk.ca_voice_play(ca, conf.voice))
    if play_code < 0 then
        sound.error_asset = name
        return play_code
    end
    return 0
end

return M
