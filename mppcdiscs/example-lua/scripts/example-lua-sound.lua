-- The demo's sound (example-cpp-sound.cpp). The music of the area the camera is in
-- (workshop, street, flat) plays on voice 1 in a forever loop, and changes only where the
-- area changes. A board clack plays on voice 2 when the lit letters change in the workshop.
-- The module keeps no state of its own: the last frame and area live in the entry's `state`.
local mathx = require("example-lua-math")
local board = require("example-lua-board")
local M = {}

-- Eye x of the camera, metres: the workshop is x < 0, the flat x > 30, the street between.
local WORKSHOP_MAX_X = 0
local FLAT_MIN_X = 30

local WORKSHOP = 0
local STREET = 1
local FLAT = 2
-- The music loop of each area, indexed by area + 1; the samples are the disc's [sounds].
local MUSIC = { "mus_factory_03.pcm", "mus_street_01.pcm", "mus_home_01.pcm" }
local CLACK = "sfx_board_clack.pcm"

local MUSIC_VOICE = 1
local CLACK_VOICE = 2

-- Envelope and volume of every voice, as example-cpp-sound.cpp: full volume.
local ATTACK = 5
local DECAY = 40
local SUSTAIN_RATE = 0
local RELEASE = 120
local SUSTAIN_LEVEL = 26000
local VOLUME = 32767

local function area_of(eye_x)
    if eye_x < WORKSHOP_MAX_X then
        return WORKSHOP
    end
    if eye_x > FLAT_MIN_X then
        return FLAT
    end
    return STREET
end

-- True when some board cell is lit in one of the two frames and not in the other.
local function letters_changed(from, to)
    for i = 0, board.CELL_COUNT - 1 do
        if board.cell_lit(i, from) ~= board.cell_lit(i, to) then
            return true
        end
    end
    return false
end

-- Plays the sample `name` on `voice` from its start. No voice_stop first: setup re-arms the voice.
local function start_sample(ca, cd, name, voice, loop)
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, name))
    if addr < 0 then
        return addr
    end
    local conf = pdk.new("rv_voice_conf")
    conf.voice = voice
    conf.loop_type = loop
    conf.sample_address = addr
    conf.ar = ATTACK
    conf.dr = DECAY
    conf.sr = SUSTAIN_RATE
    conf.rr = RELEASE
    conf.sl = SUSTAIN_LEVEL
    conf.volume = VOLUME
    conf.volume_l = VOLUME
    conf.volume_r = VOLUME
    local setup_code = tonumber(pdk.ca_voice_setup(ca, conf))
    if setup_code < 0 then
        return setup_code
    end
    return tonumber(pdk.ca_voice_play(ca, voice))
end

-- Plays what frame `frame` (in [0, PERIOD)) needs after state.sound_frame. The same frame
-- again plays nothing. Returns 0 or the first drive or ca error; on a failed music start the
-- state is not written, so the next frame retries it.
function M.update(ca, cd, keys, frame, board_present, state)
    if frame == state.sound_frame then
        return 0
    end
    local area = area_of(mathx.camera_of(keys, frame).x)
    local music_starts = state.sound_frame < 0 or area ~= state.sound_area
    local clack = board_present and state.sound_frame >= 0 and area == WORKSHOP
        and letters_changed(state.sound_frame, frame)
    if music_starts then
        local music_code = start_sample(ca, cd, MUSIC[area + 1], MUSIC_VOICE, pdk.LOOP_FOREVER)
        if music_code < 0 then
            return music_code
        end
    end
    state.sound_frame = frame
    state.sound_area = area
    if clack then
        local clack_code = start_sample(ca, cd, CLACK, CLACK_VOICE, pdk.LOOP_NONE)
        if clack_code < 0 then
            return clack_code
        end
    end
    return 0
end

return M
