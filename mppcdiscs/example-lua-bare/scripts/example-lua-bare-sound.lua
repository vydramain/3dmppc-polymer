-- The demo's music (example-cpp-sound.cpp, music and loop parts): the bars of the area the camera is in play on
-- voice 1, one bar per second, two slots in turn; the voice's repeat and mark hand it on, and a new area restarts.
-- A board clack plays on voice 4 when the lit letters change in the workshop. Two loops play on voices 8 and 16
-- while the camera is in the workshop. The footsteps are in example-lua-bare-steps.lua.
-- The tables live in the entry's `state.sound`; the read buffer of write_slot is the one module local.
local mathx = require("example-lua-bare-math")
local board = require("example-lua-bare-board")
local steps = require("example-lua-bare-steps")
local M = {}

-- Eye x of the camera, metres, as scenes/main.scene.toml places the objects: the factory is at x < 0,
-- the house facade stands at x = 33.8 with the flat behind it.
local WORKSHOP_MAX_X = 0
local FLAT_MIN_X = 33.8

-- One bar is one second of music and the frame clock is 60 frames per second: a bar starts on an exact frame.
local BAR_FRAMES = 60

local WORKSHOP = 0
local STREET = 1
local FLAT = 2

-- The bars of each area in playing order, indexed by area + 1; a list wraps. The samples are the disc's [sounds].
local BARS = {
    { "mus_factory_01.pcm", "mus_factory_02.pcm", "mus_factory_03.pcm" },
    { "mus_street_01.pcm", "mus_street_02.pcm", "mus_street_03.pcm", "mus_street_04.pcm", "mus_street_05.pcm" },
    { "mus_home_01.pcm", "mus_home_02.pcm", "mus_home_03.pcm", "mus_home_04.pcm", "mus_home_05.pcm",
        "mus_home_06.pcm", "mus_home_07.pcm", "mus_home_08.pcm", "mus_home_09.pcm", "mus_home_10.pcm" },
}
local CLACK = "sfx_board_clack.pcm"

-- Two music slots: the music voice reads one while the other is filled with the next bar. The clack has its own voice.
local SLOT_COUNT = 2
local MUSIC_VOICE = 1 -- voice 1 is mask value 1 (bit 0): at_mark tests it as `marked % (2 * MUSIC_VOICE) >= MUSIC_VOICE`
local CLACK_VOICE = 4

-- rv_err.h codes the module returns itself.
local ERR_NOMEM = -2
local ERR_INVAL = -1
local ERR_IO = -5

local MAX_LEVEL = 32767

-- The read buffer, made by read_buffer.
local scratch = nil

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

local function area_of(eye_x)
    if eye_x < WORKSHOP_MAX_X then
        return WORKSHOP
    end
    if eye_x > FLAT_MIN_X then
        return FLAT
    end
    return STREET
end

-- The area of frame `frame`, by its camera. Computed per call: no per-frame table is kept.
local function area_at(keys, frame)
    return area_of(mathx.camera_of(keys, frame).x)
end

-- The first frame of the span holding `frame`: the last span start at or before it, or else the last
-- start, which wraps from the end of the period.
local function span_start_of(starts, frame)
    local start = starts[#starts]
    for _, s in ipairs(starts) do
        if s <= frame then
            start = s
        end
    end
    return start
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

-- Records `asset` as the failure's name for the caller's log and returns `code`.
local function fail(sound, asset, code)
    sound.error_asset = asset
    return code
end

-- The read buffer, made once. A module local, not state: the console's state walk refuses cdata
-- (rv_pccl_luajit_shapewalk.cpp, legal_state_type). A reload drops it; the next call makes it again.
local function read_buffer(sound)
    if scratch == nil then
        local ok, buf = pcall(pdk.new, "uint8_t[?]", sound.largest)
        if not ok or buf == nil then
            return nil
        end
        scratch = buf
    end
    return scratch
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

-- Makes slot `index` hold bar `name`; callers write only a slot the music voice is not reading. A failed write is retried.
local function write_slot(ca, cd, sound, index, name)
    local slot = sound.slots[index]
    if slot.name == name then
        return 0
    end
    slot.name = nil
    local handle = tonumber(pdk.cd_asset_open(cd, name))
    if handle < 0 then
        return fail(sound, name, handle)
    end
    local size = tonumber(pdk.cd_asset_size(cd, handle))
    if size < 0 then
        return fail(sound, name, size)
    end
    -- An empty bar or one larger than the slot is a bad disc: the asset is malformed for the slot.
    if size == 0 or size > sound.largest then
        return fail(sound, name, ERR_INVAL)
    end
    -- As Solid Maid (sm_sound.cpp:184-186): the return of read is the length of the bar.
    local buf = read_buffer(sound)
    if buf == nil then
        return fail(sound, name, ERR_NOMEM)
    end
    local read = tonumber(pdk.cd_asset_read(cd, handle, buf, size))
    if read <= 0 then
        return fail(sound, name, ERR_IO)
    end
    local sample = pdk.new("rv_sample")
    sample.data = buf
    sample.size = read
    local write_code = tonumber(pdk.ca_sound_asset_write(ca, slot.addr, sample))
    if write_code < 0 then
        return fail(sound, name, write_code)
    end
    slot.name = name
    return 0
end

local function other_slot(index)
    return index % SLOT_COUNT + 1
end

local function bar_name(song, bar)
    return song[bar % #song + 1]
end

-- Sets the voice to go on into slot `index` at its bar end and marks that slot's start; both are repeated on a retry.
local function arm(ca, sound, index)
    local slot = sound.slots[index]
    local repeat_code = tonumber(pdk.ca_voice_repeat(ca, MUSIC_VOICE, slot.addr))
    if repeat_code < 0 then
        return fail(sound, slot.name, repeat_code)
    end
    local mark_code = tonumber(pdk.ca_voice_mark(ca, MUSIC_VOICE, slot.addr))
    if mark_code < 0 then
        return fail(sound, slot.name, mark_code)
    end
    return 0
end

-- Writes the queued bar into music_slot and arms its mark: the last step of a chain start. Retried until both succeed.
local function arm_chain(ca, cd, sound)
    local write_code = write_slot(ca, cd, sound, sound.music_slot, sound.music_name)
    if write_code < 0 then
        return write_code
    end
    local arm_code = arm(ca, sound, sound.music_slot)
    if arm_code < 0 then
        return arm_code
    end
    sound.music_armed = true
    return 0
end

-- Reads whether the music voice has reached its mark: its bit of the marked mask. Returns the code and the flag.
local function at_mark(ca, sound)
    local marked = tonumber(pdk.ca_voice_marked(ca, MUSIC_VOICE))
    if marked < 0 then
        return fail(sound, "music mark", marked), false
    end
    return 0, marked % (2 * MUSIC_VOICE) >= MUSIC_VOICE
end

-- Starts bar `bar` of the area's song; without `play` it only fills the slots. A running chain restarts on the free slot.
local function start_chain(ca, cd, sound, area, bar, play)
    local song = BARS[area + 1]
    local free = bar % SLOT_COUNT + 1
    if sound.music_on then
        local mark_code, reached = at_mark(ca, sound)
        if mark_code < 0 then
            return mark_code
        end
        if reached then
            free = other_slot(sound.music_slot)
        else
            free = sound.music_slot
        end
    end
    local next_slot = other_slot(free)
    local now_code = write_slot(ca, cd, sound, free, bar_name(song, bar))
    if now_code < 0 then
        return now_code
    end
    if not play then
        return write_slot(ca, cd, sound, next_slot, bar_name(song, bar + 1))
    end
    local slot = sound.slots[free]
    local play_code = play_voice(ca, voice_conf(MUSIC_VOICE, pdk.LOOP_FOREVER, slot.addr, MUSIC_ENVELOPE))
    if play_code < 0 then
        return fail(sound, slot.name, play_code)
    end
    -- The voice reads `free` from here: a failed step is finished by arm_chain, a restart could write that slot.
    sound.music_on = true
    sound.music_area = area
    sound.music_armed = false
    sound.music_slot = next_slot
    sound.music_bar = bar + 1
    sound.music_name = bar_name(song, bar + 1)
    return arm_chain(ca, cd, sound)
end

-- The voice reached its mark: the slot it left takes the bar after the queued one; the mark moves once armed.
local function advance(ca, cd, sound, area)
    local song = BARS[area + 1]
    local free = other_slot(sound.music_slot)
    local queued = sound.music_bar + 1
    local write_code = write_slot(ca, cd, sound, free, bar_name(song, queued))
    if write_code < 0 then
        return write_code
    end
    local arm_code = arm(ca, sound, free)
    if arm_code < 0 then
        return arm_code
    end
    sound.music_slot = free
    sound.music_bar = queued
    sound.music_name = bar_name(song, queued)
    return 0
end

local function play_clack(ca, cd, sound)
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, CLACK))
    if addr < 0 then
        return fail(sound, CLACK, addr)
    end
    local conf = voice_conf(CLACK_VOICE, pdk.LOOP_NONE, addr, CLACK_ENVELOPE)
    -- local play_code = play_voice(ca, conf)
    -- if play_code < 0 then
    --     return fail(sound, CLACK, play_code)
    -- end
    return 0
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
        if code == 0 and loop_code < 0 then
            code = loop_code
            asset = sound.error_asset
        end
    end
    sound.error_asset = asset
    return code
end

-- Frees the first `count` music slots of a failed init; a slot that will not free is logged, the init error is kept.
local function free_slots(ca, sound, count)
    for index = 1, count do
        local free_code = tonumber(pdk.ca_sound_asset_free(ca, sound.slots[index].addr))
        if free_code < 0 then
            print(string.format("example-lua-bare: music slot not freed: rv_err %d", free_code))
        end
    end
end

-- Builds the music slots and the read buffer, checks the bar sizes and lists the span starts once; areas are computed per
-- frame by area_at. Returns 0 or the first error, with
-- state.sound.error_asset naming the asset. state.sound must already exist (example-lua-bare's disc_initialize).
function M.init(ca, cd, keys, state)
    local sound = state.sound
    -- The slots are sized to the largest bar, measured off the disc: a re-recorded bar must not overflow them.
    local largest = 0
    for _, song in ipairs(BARS) do
        for _, name in ipairs(song) do
            local handle = tonumber(pdk.cd_asset_open(cd, name))
            if handle < 0 then
                return fail(sound, name, handle)
            end
            local size = tonumber(pdk.cd_asset_size(cd, handle))
            if size < 0 then
                return fail(sound, name, size)
            end
            if size == 0 then
                return fail(sound, name, ERR_INVAL)
            end
            largest = math.max(largest, size)
        end
    end
    sound.largest = largest
    if read_buffer(sound) == nil then
        return fail(sound, "read buffer", ERR_NOMEM)
    end
    for index = 1, SLOT_COUNT do
        local addr = tonumber(pdk.ca_sound_asset_malloc(ca, largest))
        if addr < 0 then
            free_slots(ca, sound, index - 1)
            return fail(sound, "music slot", addr)
        end
        sound.slots[index] = { addr = addr }
    end

    -- The frames where an area span starts, ascending. A span starts where its area differs from the
    -- frame before it, cyclically.
    local period = mathx.PERIOD_FRAMES
    local starts = {}
    for f = 0, period - 1 do
        if area_at(keys, f) ~= area_at(keys, (f + period - 1) % period) then
            starts[#starts + 1] = f
        end
    end
    -- With no change at all the one area covers the period and its span starts at frame 0.
    if #starts == 0 then
        starts[1] = 0
    end
    sound.starts = starts
    sound.music_on = false
    sound.music_armed = false
    sound.loop_armed = {}
    for index = 1, #LOOPS do
        sound.loop_armed[index] = false
    end
    steps.init(keys)
    sound.ready = true
    return 0
end

-- Plays what frame `frame` (in [0, PERIOD)) needs after state.sound.frame. The same frame again plays
-- nothing, and nothing plays before init. Returns 0 or the first error, with state.sound.error_asset
-- naming the asset. A failed music start is not committed, so the next update call retries it: the next
-- frame, or this frame again while paused.
function M.update(ca, cd, keys, frame, board_present, state)
    local sound = state.sound
    if not sound.ready then
        return 0
    end
    -- Refused, as example-cpp refuses a table not period-sized: the steps would index past its end.
    local table_of = steps.distance(keys)
    if #table_of ~= mathx.PERIOD_FRAMES then
        return fail(sound, "sound tables", ERR_INVAL)
    end
    if frame == sound.frame then
        return 0
    end
    local period = mathx.PERIOD_FRAMES
    local area = area_at(keys, frame)
    local span_offset = (frame - span_start_of(sound.starts, frame) + period) % period
    local bar = math.floor(span_offset / BAR_FRAMES)
    -- Both are decided against the previous frame before sound takes this one.
    local first = sound.frame < 0
    local area_changed = first or area ~= sound.area
    -- A first frame mid-bar fills the slots and waits (a running chain restarts instead): the bar is not played from its head.
    local silent = first and not sound.music_on and span_offset % BAR_FRAMES ~= 0
    local clack = board_present and not first and area == WORKSHOP and letters_changed(sound.frame, frame)
    -- Every new frame runs the loops. The clack runs only after the music step succeeded: otherwise the same clack would
    -- repeat every frame. The first failure is returned with its asset.
    local code = 0
    local asset = ""
    local music_code = 0
    if sound.music_on and not sound.music_armed then
        -- A failed start is finished, not restarted; if the area changed since it started, it starts again.
        music_code = arm_chain(ca, cd, sound)
        if music_code == 0 and area ~= sound.music_area then
            music_code = start_chain(ca, cd, sound, area, bar, true)
        end
    elseif area_changed or (not sound.music_on and bar ~= sound.bar) then
        music_code = start_chain(ca, cd, sound, area, bar, not silent)
    elseif sound.music_on then
        local reached = false
        music_code, reached = at_mark(ca, sound)
        if music_code == 0 and reached then
            music_code = advance(ca, cd, sound, area)
        end
    end
    if music_code < 0 then
        code = music_code
        asset = sound.error_asset
    end
    -- Committed only after the music start: a failed start is retried by the next update call.
    local music_ok = music_code == 0
    if music_ok then
        sound.frame = frame
        sound.area = area
        sound.bar = bar
    end
    local loops_code = set_loops(ca, cd, sound, area == WORKSHOP)
    if code == 0 and loops_code < 0 then
        code = loops_code
        asset = sound.error_asset
    end
    if music_ok and clack then
        local clack_code = play_clack(ca, cd, sound)
        if code == 0 and clack_code < 0 then
            code = clack_code
            asset = sound.error_asset
        end
    end
    -- Only once the frame is committed: a failed music start is retried by the next update call (the same frame
    -- again while paused), and the step of a committed frame must not repeat.
    if music_ok then
        -- local steps_code = steps.update(ca, cd, table_of, frame, area, sound)
        local steps_code = 0
        if code == 0 and steps_code < 0 then
            code = steps_code
            asset = sound.error_asset
        end
    end
    sound.error_asset = asset
    return code
end

return M
