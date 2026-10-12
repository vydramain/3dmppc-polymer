-- The demo's music (example-cpp-sound.cpp, music part): the bars of the area the camera is in
-- play on voice 1 and 2 in turn, one bar per second, and a bar starts only where the bar or the
-- area changes. A board clack plays on voice 4 when the lit letters change in the workshop.
-- The tables live in the entry's `state.sound`; the read buffer of write_slot is the one module local.
local mathx = require("example-lua-math")
local board = require("example-lua-board")
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

-- Music slot s is voice s; a bar goes to the slot of its index parity. The clack has its own voice.
local SLOT_COUNT = 2
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
-- Clack: the shape example-lua gives its tone, at full volume.
local CLACK_ENVELOPE = { ar = 5, dr = 40, sr = 0, rr = 120, sl = 26000, volume = MAX_LEVEL }

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

-- True when the slot's voice is still playing at `frame`: a bar lasts one bar, the last frame included,
-- since the sample runs a little past 60 frames.
-- Only a playing voice can be stopped.
local function sounding(slot, frame)
    if not slot.armed then
        return false
    end
    return (frame - slot.frame + mathx.PERIOD_FRAMES) % mathx.PERIOD_FRAMES <= BAR_FRAMES
end

-- Makes slot `index` hold bar `name`. A voice still playing may read the slot, so it is stopped
-- before the slot is overwritten. The slot is unknown while the write runs, so a failed write is retried.
local function write_slot(ca, cd, sound, index, frame, name)
    local slot = sound.slots[index]
    if slot.name == name then
        return 0
    end
    if sounding(slot, frame) then
        local stop_code = tonumber(pdk.ca_voice_stop(ca, index))
        if stop_code < 0 then
            return fail(sound, name, stop_code)
        end
        slot.armed = false
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

-- Starts bar `bar` of the area's song when `play` is set, counted from the area span's first frame, and
-- fills the other slot with the next bar. The bar's own slot is filled first when it is not yet.
local function play_bar(ca, cd, sound, area, bar, frame, play)
    local song = BARS[area + 1]
    local index = bar % SLOT_COUNT + 1
    local next_index = index % SLOT_COUNT + 1
    local now_name = song[bar % #song + 1]
    local next_name = song[(bar + 1) % #song + 1]

    local now_code = write_slot(ca, cd, sound, index, frame, now_name)
    if now_code < 0 then
        return now_code
    end
    if play then
        local slot = sound.slots[index]
        local play_code = play_voice(ca, voice_conf(index, pdk.LOOP_NONE, slot.addr, MUSIC_ENVELOPE))
        if play_code < 0 then
            return fail(sound, now_name, play_code)
        end
        slot.armed = true
        slot.frame = frame
    end
    -- Written ahead: a failure costs nothing now, since the slot is filled at the next bar start.
    local next_code = write_slot(ca, cd, sound, next_index, frame, next_name)
    if next_code < 0 and not sound.write_ahead_logged then
        sound.write_ahead_logged = true
        print(string.format("example-lua: %s not written ahead: rv_err %d, filled at the next bar start",
            next_name, next_code))
    end
    return 0
end

local function play_clack(ca, cd, sound)
    local addr = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, CLACK))
    if addr < 0 then
        return fail(sound, CLACK, addr)
    end
    local play_code = play_voice(ca, voice_conf(CLACK_VOICE, pdk.LOOP_NONE, addr, CLACK_ENVELOPE))
    if play_code < 0 then
        return fail(sound, CLACK, play_code)
    end
    return 0
end

-- Frees the first `count` music slots of a failed init; a slot that will not free is logged, the init error is kept.
local function free_slots(ca, sound, count)
    for index = 1, count do
        local free_code = tonumber(pdk.ca_sound_asset_free(ca, sound.slots[index].addr))
        if free_code < 0 then
            print(string.format("example-lua: music slot not freed: rv_err %d", free_code))
        end
    end
end

-- Builds the music slots and the read buffer, checks the bar sizes and lists the span starts once; areas are computed per
-- frame by area_at. Returns 0 or the first error, with
-- state.sound.error_asset naming the asset. state.sound must already exist (example-lua's disc_initialize).
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
        sound.slots[index] = { addr = addr, armed = false, frame = 0 }
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
    sound.ready = true
    return 0
end

-- Plays what frame `frame` (in [0, PERIOD)) needs after state.sound.frame. The same frame again plays
-- nothing, and nothing plays before init. Returns 0 or the first error, with state.sound.error_asset
-- naming the asset. A failed music start is not committed, so the next frame retries it.
function M.update(ca, cd, keys, frame, board_present, state)
    local sound = state.sound
    if not sound.ready or frame == sound.frame then
        return 0
    end
    local period = mathx.PERIOD_FRAMES
    local area = area_at(keys, frame)
    local span_offset = (frame - span_start_of(sound.starts, frame) + period) % period
    local bar = math.floor(span_offset / BAR_FRAMES)
    -- Both are decided against the previous frame before sound takes this one.
    local first = sound.frame < 0
    local area_changed = first or area ~= sound.area
    local bar_starts = area_changed or bar ~= sound.bar
    -- A first frame mid-bar fills the slots and waits: the bar it is in is not played from its head.
    local silent = first and span_offset % BAR_FRAMES ~= 0
    local clack = board_present and not first and area == WORKSHOP and letters_changed(sound.frame, frame)
    if bar_starts then
        local music_code = play_bar(ca, cd, sound, area, bar, frame, not silent)
        if music_code < 0 then
            return music_code
        end
    end
    sound.frame = frame
    sound.area = area
    sound.bar = bar
    -- The clack runs only after the music step succeeded: otherwise the same clack would repeat every frame.
    if clack then
        local clack_code = play_clack(ca, cd, sound)
        if clack_code < 0 then
            return clack_code
        end
    end
    return 0
end

return M
