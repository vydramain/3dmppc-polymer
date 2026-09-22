-- example-lua's entry chunk. The disc's own hooks (src/example-lua.cpp) each
-- forward one-to-one into a same-named function here, so this file mirrors
-- the disc's shape instead of inventing one of its own.
--
-- Nothing here touches _G: the chunk returns a module table instead, and the
-- disc calls into it by name through rv_cl_script_call(cl, chunk_, "...").
-- A global would work by accident today (one script, one VM) but would
-- collide the moment a disc raises more than one chunk into the same
-- machine.
local M = {}

-- `state` is not declared anywhere in this file: the console places its ONE
-- persistent table into this chunk's own environment, under this plain name,
-- before any hook of this chunk ever runs - once at boot, right after this
-- chunk is raised, and again after every successful code reload. An
-- undeclared identifier is exactly how Lua spells "look this up in my
-- environment", and the console's environment for this chunk resolves
-- `state` to that one table; nothing here has to ask for it. This is the
-- ONLY thing that survives a reload: a chunk local or a field of M dies with
-- the code that reload replaces. Everything this script needs to keep is a
-- field of `state`, never a local and never a field of M.
--
-- This script declares no shape for it: the console learns one for itself,
-- once, right after disc_initialize below has returned - by then `state`
-- holds exactly the fields this script keeps, which is all the console needs
-- to hold a later reload candidate to (see README.md's "Code != state").
--
-- Not stored: a function or a coroutine. Either would keep the OLD chunk's
-- bytecode alive and callable after the swap - the state table would quietly
-- carry a piece of code the reload was supposed to have replaced. This
-- script has no closure worth surviving a reload (set_vertex below is
-- recreated each frame_render, cheaply, from data already in state).

-- The one texture this script draws. It never acquires or releases it - it
-- only ever names it. The drive makes it resident the first time
-- pdk.resource_resolve (pdklib, called below in frame_render) asks for this
-- name, keeps it resident and refreshes it in place on a dev reload, and
-- frees it itself when this disc unloads, so this script has nothing to
-- hold onto but the name.
local ASSET_TEXTURE_NAME = "example-sprite.mppctex"

-- The one sound this script plays. Same idea as ASSET_TEXTURE_NAME above,
-- but AUDIO has no baked container (disc.toml lists it under [assets], not
-- [textures] - the PR review's decision was raw PCM with no baker), so the
-- bytes on the disc ARE the bytes the drive makes resident; nothing decodes
-- them first. This name is asked for once, in disc_initialize below, not
-- every frame like the sprite: a voice, once armed, keeps its own resolved
-- address and does not need re-resolving the way a per-frame sprite draw
-- does.
local ASSET_TONE_NAME = "example-tone.pcm"

-- Printed while the CHUNK BODY runs, i.e. already during rv_cl_script_entry's
-- first raise - before disc_initialize or any other hook is ever called.
-- print() is routed into the console's stderr logger, not stdout, so this is
-- a "the bytecode executed" breadcrumb, never something a test reads.
--
-- Nothing else runs here: the body must stay PURE (no pdk call, no write to
-- `state`) because it also runs to VALIDATE a reload candidate, before the
-- console knows whether the candidate will pass its shape check. `state` is
-- the SAME table the currently running code depends on, reachable here too,
-- so a write at this level does not merely "leave an effect" - it corrupts
-- the old code's own data. The console can undo that if this candidate is
-- then refused, but a body that stays pure never needs it to.
print("Hello from example lua!")

-- The disc author's decision point: this is where a game picks what it
-- STARTS AS - a menu, a saved game's last scene, a character-creation
-- screen, a demo loop - by calling or setting up whatever that requires.
-- Nothing above this function makes that choice, and nothing else in this
-- chunk runs before it does.
--
-- Not the same thing as RV_MPPC_LUA_DISC_DEF("example-lua")'s own generated
-- disc_initialize (src/example-lua.cpp), which shares this name but does a
-- different job: it raises this chunk with rv_cl_script_entry() and then
-- forwards every lifecycle hook into its same-named Lua function, this one
-- included. That macro IS the technical wiring - legal names, the rv_cl call
-- shape, handing the organizer handle across - Lua exists so a disc author
-- never has to write it. This function is the first place that wiring hands
-- control to actual game content.
--
-- `o_` is that organizer handle (an rv_pdko*), passed into EVERY hook of
-- this chunk by the console, not something this script asked for. It is not
-- a controller itself: pdk.cv(o_), pdk.cd(o_), and pdk.ca(o_)/pdk.cio(o_)
-- the same way, resolve it into the actual controller a call needs, so each
-- hook only reaches for the ones it uses. Explained here, once, because
-- every hook below takes the same `o_` for the same reason.
function M.disc_initialize(o_)
	local cv = pdk.cv(o_)

	-- Nothing inserts a default for this any more - the console only WATCHES
	-- `state`'s shape now, it does not manufacture values for it - so the
	-- field this example exists to demonstrate has to start at 0 here, the
	-- one time disc_initialize ever runs, or frame_update below finds nil.
	state.frame_count = 0

	-- Read something real back through pdk and log it: the headless-
	-- verifiable proof that a Lua call reached the console's own
	-- rv_cv_screen_width and got its real answer, not a stub. Stored in
	-- state, not a local, because frame_render (below) needs it and a local
	-- set here would not survive a reload.
	state.screen_width = tonumber(pdk.cv_screen_width(cv))
	state.screen_height = tonumber(pdk.cv_screen_height(cv))
	print(string.format("example-lua: screen is %dx%d (read through pdk)", state.screen_width, state.screen_height))

	-- Nothing to acquire here: the drive makes ASSET_TEXTURE_NAME resident
	-- the first time frame_render names it, not before.

	-- The tone, by contrast, is acquired right here: rv_cd_resource_addr and
	-- rv_cd_resource_size (both against RV_CD_RESOURCE_AUDIO) are what makes
	-- ASSET_TONE_NAME resident in sound RAM - this script never calls
	-- rv_ca_sound_asset_malloc itself, the drive owns that the same way it
	-- owns video RAM for a texture. tonumber() unwraps the cdata int64_t
	-- both calls return, the same way state.screen_width did above.
	local cd = pdk.cd(o_)
	local ca = pdk.ca(o_)
	local addr_tone = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_AUDIO, ASSET_TONE_NAME))
	local size_tone = tonumber(pdk.cd_resource_size(cd, pdk.CD_RESOURCE_AUDIO, ASSET_TONE_NAME))
	print(string.format("example-lua: tone resident at addr=%d size=%d byte(s)", addr_tone, size_tone))

	-- The cross-kind proof this task exists to make: a query that does not
	-- describe the kind it was asked of must answer a negative rv_err, not a
	-- fabricated 0 that would read back as "no palette" or "0 bytes". Both
	-- probes below are expected to be negative.
	local texture_query_on_audio_name = tonumber(pdk.cd_resource_palette_addr(cd, pdk.CD_RESOURCE_TEXTURE, ASSET_TONE_NAME))
	local size_query_on_texture_name = tonumber(pdk.cd_resource_size(cd, pdk.CD_RESOURCE_TEXTURE, ASSET_TEXTURE_NAME))
	print(string.format(
		"example-lua: cross-kind probe: texture-query(audio name)=%d size-query(texture name)=%d",
		texture_query_on_audio_name, size_query_on_texture_name))

	if addr_tone >= 0 and ca ~= nil and tonumber(pdk.ca_voice_count(ca)) >= 1 then
		-- Same ADSR shape rv_dmain_setup.cpp's own build_beep() uses for its
		-- built-in beep - a known-good envelope, not a value this example
		-- invented. Set field by field, the way set_vertex above fills a
		-- vertex, rather than a positional pdk.new(...) table: a voice
		-- config has eleven fields and reads better named than counted.
		local voice = pdk.new("rv_voice_conf")
		voice.voice = 1 -- voice 0
		voice.loop_type = pdk.LOOP_NONE
		voice.sample_address = addr_tone
		voice.ar = 5
		voice.dr = 40
		voice.sr = 0
		voice.rr = 120
		voice.sl = 26000
		voice.volume = 32767
		voice.volume_l = 32767
		voice.volume_r = 32767
		if tonumber(pdk.ca_voice_setup(ca, voice)) >= 0 then
			pdk.ca_voice_play(ca, 1)
		end
	end
end
function M.frame_update(dt, o_)
	-- The frame counter: the one field this example exists to demonstrate.
	-- It lives in `state`, so a code reload (the console re-wires `state`
	-- into the new code's environment; this chunk's locals do not survive)
	-- leaves it exactly where it was - the count CONTINUES instead of
	-- restarting at 0.
	state.frame_count = state.frame_count + 1

	-- Should the disc stop? this example does not wire a button up to check
	-- - false every frame is still the honest answer for a script that
	-- raises no stop condition of its own.
	return false
end

function M.frame_render(o_)
	local cv = pdk.cv(o_)

	-- Same shape as example-cpp.cpp's own frame_render: configure the frame
	-- (clear colour), fill it with a primitive, and leave the flush to the
	-- disc's C++ side (src/example-lua.cpp), which calls rv_cv_frame_flush
	-- right after this hook returns.
	pdk.cv_frame_configure(cv, 0, pdk.new("rv_color", { 20, 24, 40 }))

	-- pdk.primitive_polygon(3) (pdklib) hands back an rv_primitive with
	-- every field the console requires from a flat-coloured triangle
	-- already set - type, fill_mode, mapping, the zeroed texture/palette
	-- pair, vertex_count; see that helper's own comment for why those are
	-- its job and not this script's. depth and the three vertices - their
	-- positions and their colours - are this game's content, so they are
	-- set right here, not in pdklib.
	local primitive = pdk.primitive_polygon(3)
	primitive.depth = 0
	local polygon = primitive.data.polygon

	local function set_vertex(i, x, y, r, g, b)
		local v = polygon.vertexes[i]
		v.x, v.y = math.floor(x), math.floor(y)
		v.color.r, v.color.g, v.color.b = r, g, b
	end

	local w, h = state.screen_width, state.screen_height
	set_vertex(0, w / 2, 40, 255, 80, 80)
	set_vertex(1, 40, h - 40, 80, 255, 80)
	set_vertex(2, w - 40, h - 40, 80, 80, 255)

	pdk.cv_frame_put(cv, primitive)

	-- The sprite next to the triangle: SAMPLE_TEXTURE against ASSET_TEXTURE_NAME,
	-- resolved by name through pdk.resource_resolve (pdklib), which is now
	-- the one place that speaks RV_CD_RESOURCE_TEXTURE and makes the four
	-- separate drive calls - see that helper's comment for why the kind is
	-- pdklib's business and the name stays this script's. Resolved fresh
	-- every frame, not cached, because an address is only valid until that
	-- texture is reloaded. A nil result (the asset missing) is this script's
	-- own decision to skip the sprite rather than crash - disc_initialize
	-- never has to refuse to start over it, and the triangle above still
	-- draws either way.
	local resolved = pdk.resource_resolve(o_, ASSET_TEXTURE_NAME)
	if resolved ~= nil then
		-- pdk.primitive_sprite (pdklib) fills in type, fill_mode, mapping
		-- and the resolved addr_texture/addr_palette, and defaults
		-- width/height to the texture's own size; see that helper's
		-- comment for why. depth, position and colour are this game's to
		-- set, so they are set right here, not in pdklib.
		local sprite_primitive = pdk.primitive_sprite(resolved)
		sprite_primitive.depth = 1

		local sprite = sprite_primitive.data.sprite
		sprite.color.r, sprite.color.g, sprite.color.b = 255, 255, 255
		sprite.x = 16
		sprite.y = 16

		pdk.cv_frame_put(cv, sprite_primitive)
	end
end

function M.disc_shutdown(o_) end

return M
