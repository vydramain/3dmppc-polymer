-- The voices of the demo's sound (example-cpp-pdkkit-sound.cpp): the envelopes of the music and the clack, the workshop
-- loops with their start and stop, the voice conf and its play, and the failure record every sound step shares.
-- Holds no state: the loops' armed flags live in the entry's `state.sound`, which the callers pass in.
local M = {}

local MAX_LEVEL = 32767

-- Music: Solid Maid sets no envelope, so the bar holds at full sustain; it is mixed at 10000 of 32767.
local MUSIC_ENVELOPE = { ar = 0, dr = 0, sr = 0, rr = 0, sl = MAX_LEVEL, volume = 10000 }
-- Clack: the shape example-lua-bare gives its tone, at full volume.
local CLACK_ENVELOPE = { ar = 5, dr = 40, sr = 0, rr = 120, sl = 26000, volume = MAX_LEVEL }

-- The conveyor loop's voice, as Solid Maid sets it.
local CONVEYOR_VOICE = 8
-- The assembly loop's voice, as Solid Maid sets it.
local ASSEMBLY_VOICE = 16
-- The conveyor loop's volume, of 32767, as Solid Maid sets it.
local CONVEYOR_VOLUME = 6600
-- The assembly loop's volume, of 32767, as Solid Maid sets it.
local ASSEMBLY_VOLUME = 12100
-- The loops' ar, dr, sr and rr: 0, as Solid Maid sets no envelope.
local LOOP_ENVELOPE = 0

-- The workshop loops, as Solid Maid sets them (sm_sound.cpp:127-129, :410-452): no envelope, full sustain,
-- looped forever. The volumes are 6600 and 12100 of 32767.
local LOOPS = {
    {
        name = "sfx_loop_assembly.pcm",
        voice = ASSEMBLY_VOICE,
        env = {
            ar = LOOP_ENVELOPE, dr = LOOP_ENVELOPE, sr = LOOP_ENVELOPE, rr = LOOP_ENVELOPE,
            sl = MAX_LEVEL, volume = ASSEMBLY_VOLUME,
        },
    },
}
-- Records `asset` as the failure's name for the caller's log and returns `code`.
local function fail(sound, asset, code)
    sound.error_asset = asset
    return code
end

-- Folds a step into the first failure: keeps `code` and `asset` once one step has failed;
-- otherwise takes the step's failure, if it has one.
local function first_error(code, asset, step_code, step_asset)
    if code == 0 and step_code < 0 then
        return step_code, step_asset
    end
    return code, asset
end

-- Sets up and starts conf.voice. No voice_stop first: setup re-arms the voice.
local function play_voice(ca, conf)
    local setup_code = tonumber(pdk.ca_voice_setup(ca, conf))
    if setup_code < 0 then
        return setup_code
    end
    return tonumber(pdk.ca_voice_play(ca, conf.voice))
end

-- A voice conf for `voice` on the sample at `addr`, with the envelope and volume of `env`.
local function voice_conf(voice, loop, addr, env)
    local conf = pdk.new("rv_voice_conf")
    conf.voice = voice
    conf.loop_type = loop
    conf.sample_address = addr
    conf.ar = env.ar
    conf.dr = env.dr
    conf.sr = env.sr
    conf.rr = env.rr
    conf.sl = env.sl
    conf.volume = env.volume
    conf.volume_l = MAX_LEVEL
    conf.volume_r = MAX_LEVEL
    return conf
end

-- Starts loop `index` when `on`, stops it otherwise. A stop is sent only to a loop this state started: the
-- console refuses a stop on a voice not armed. A loop already in the wanted state does nothing.
local function set_loop(ca, cd, sound, index, on)
    local loop = LOOPS[index]
    if on == sound.loop_armed[index] then
        return 0
    end
    if not on then
        local stop_code = tonumber(pdk.ca_voice_stop(ca, loop.voice))
        if stop_code < 0 then
            return fail(sound, loop.name, stop_code)
        end
        sound.loop_armed[index] = false
        return 0
    end
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, loop.name))
    if addr < 0 then
        return fail(sound, loop.name, addr)
    end
    local play_code = play_voice(ca, voice_conf(loop.voice, pdk.LOOP_FOREVER, addr, loop.env))
    if play_code < 0 then
        return fail(sound, loop.name, play_code)
    end
    sound.loop_armed[index] = true
    return 0
end

-- Keeps both loops playing while `in_workshop`, silent otherwise. Both are tried; the first failure is returned
-- with its asset.
local function set_loops(ca, cd, sound, in_workshop)
    local code = 0
    local asset = ""
    for index = 1, #LOOPS do
        local loop_code = set_loop(ca, cd, sound, index, in_workshop)
        code, asset = first_error(code, asset, loop_code, sound.error_asset)
    end
    sound.error_asset = asset
    return code
end

M.MUSIC_ENVELOPE = MUSIC_ENVELOPE
M.CLACK_ENVELOPE = CLACK_ENVELOPE
M.LOOP_COUNT = #LOOPS
M.fail = fail
M.first_error = first_error
M.play_voice = play_voice
M.voice_conf = voice_conf
M.set_loops = set_loops

return M
