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

-- Bumped whenever a stored shape changes. attach() below refuses a state
-- table whose "version" field it has no migration for, INSTEAD of guessing -
-- an old field silently misread as a new one is a worse failure than a
-- refused reload.
local STATE_VERSION = 2

-- The console owns ONE persistent table for the whole run and hands it to
-- M.attach() - once at boot, right after this chunk is raised, and again
-- after every successful code reload. This is the ONLY thing that survives a
-- reload: a chunk local (like this one) or a field of M dies with the code
-- that reload replaces. Everything this script needs to keep is a field of
-- `state`, never a local and never a field of M.
--
-- Not stored: a function or a coroutine. Either would keep the OLD chunk's
-- bytecode alive and callable after the swap - the state table would quietly
-- carry a piece of code the reload was supposed to have replaced. This
-- script has no closure worth surviving a reload (set_vertex below is
-- recreated each frame_render, cheaply, from data already in state).
local state

-- The one asset this script OWNS: it acquires it once, in disc_initialize,
-- remembers only the residency id the drive hands back, and it is the only
-- asset name M.asset_changed (below) will accept. "Owns" is the whole
-- division of labour behind asset reload - the drive decodes and re-decodes
-- the container, but only the game knows which primitives draw with it.
local ASSET_TEXTURE_NAME = "example-sprite.mppctex"

-- Printed while the CHUNK BODY runs, i.e. already during rv_cl_script_entry's
-- first raise - before disc_initialize, attach, or any other hook is ever
-- called. print() is routed into the console's stderr logger, not stdout, so
-- this is a "the bytecode executed" breadcrumb, never something a test reads.
--
-- Nothing else runs here: the body must stay PURE (no pdk call, no write to
-- `state`) because it also runs to VALIDATE a reload candidate, before the
-- console knows whether attach() will accept it. An impure body would leave
-- effects behind even for a candidate that is about to be refused.
print("Hello from example lua!")

function M.attach(s)
	-- Compatibility check FIRST, before a single field is written - a
	-- refusal must leave the state exactly as the old code left it.
	if s.version ~= nil and s.version > STATE_VERSION then
		return false, string.format("state version %d is newer than this code (%d)", s.version, STATE_VERSION)
	end

	-- One real migration, so the shape exists rather than being described
	-- in a comment: version 1 kept the frame counter under the old name
	-- "frames" - version 2 renamed it to "frame_count" for clarity. A
	-- table from a version-1 chunk still attaches; it is upgraded in place.
	if s.version == 1 and s.frame_count == nil then
		s.frame_count = s.frames
		s.frames = nil
	end
	s.version = STATE_VERSION

	-- Explicit nil checks, not `x = x or default`: false/0 are legal stored
	-- values and `or` would stomp them back to the default every attach().
	if s.frame_count == nil then
		s.frame_count = 0
	end
	if s.screen_width == nil then
		s.screen_width = false
	end
	if s.screen_height == nil then
		s.screen_height = false
	end

	state = s
	return true
end

function M.disc_initialize(o_)
	local o = pdk.cast("rv_pdko*", o_)
	local cv = pdk.pdko_cv(o)
	local cd = pdk.pdko_cd(o)

	-- The one pointer kept across a reload: M.asset_changed (below) is called
	-- by the console with only a name and no handle, so until the console
	-- owns asset reload this is what it re-derives cv/cd from.
	state.pdko = o_

	-- Read something real back through pdk and log it: the headless-
	-- verifiable proof that a Lua call reached the console's own
	-- rv_cv_screen_width and got its real answer, not a stub. Stored in
	-- state, not a local, because frame_render (below) needs it and a local
	-- set here would not survive a reload.
	state.screen_width = tonumber(pdk.cv_screen_width(cv))
	state.screen_height = tonumber(pdk.cv_screen_height(cv))
	print(string.format("example-lua: screen is %dx%d (read through pdk)", state.screen_width, state.screen_height))

	-- Acquired exactly once here, never in attach(): disc_initialize is the
	-- only hook that runs at boot and never again, and it is the residency id
	-- it gets back NOW that M.asset_changed (below) has to remember across
	-- every future code reload - hence storing it in `state`, not a local.
	local res = pdk.cd_texture_acquire(cd, ASSET_TEXTURE_NAME)
	if res < 0 then
		print("example-lua: could not acquire texture '" .. ASSET_TEXTURE_NAME .. "'")
		return
	end

	state.tex_name = ASSET_TEXTURE_NAME
	state.tex_res = tonumber(res)
end

-- The console's asset-reload hook: called on the ENTRY CHUNK, at a frame
-- boundary, with the flat entry name whose bytes on the drive just changed.
-- true means this script has already re-read and re-uploaded it; false plus
-- a reason is a refusal, reported to the client as an error, and leaves the
-- old video data exactly where it was.
--
-- Only the ONE asset this script acquired in disc_initialize (above) is
-- ever accepted - a name it does not own is refused without touching
-- anything. The drive owns the decode and the video allocation now, so a
-- texture that changed size is just a fresh acquire, not a refusal.
function M.asset_changed(name)
	if name ~= state.tex_name then
		return false, "asset '" .. tostring(name) .. "' is not owned by this script"
	end

	local cd = pdk.pdko_cd(pdk.cast("rv_pdko*", state.pdko))

	pdk.cd_texture_release(cd, state.tex_res)
	local res = pdk.cd_texture_acquire(cd, name)
	if res < 0 then
		state.tex_res = nil
		return false, "could not re-acquire texture '" .. name .. "'"
	end

	state.tex_res = tonumber(res)
	return true
end

function M.frame_update(dt, o_)
	-- The frame counter: the one field this example exists to demonstrate.
	-- It lives in `state`, so a code reload (M.attach runs, this chunk's
	-- locals do not) leaves it exactly where it was - the count CONTINUES
	-- instead of restarting at 0.
	state.frame_count = state.frame_count + 1

	-- Should the disc stop? this example does not wire a button up to check
	-- - false every frame is still the honest answer for a script that
	-- raises no stop condition of its own.
	return false
end

function M.frame_render(o_)
	local cv = pdk.pdko_cv(pdk.cast("rv_pdko*", o_))

	-- Same shape as example-cpp.cpp's own frame_render: configure the frame
	-- (clear colour), fill it with a primitive, and leave the flush to the
	-- disc's C++ side (src/example-lua.cpp), which calls rv_cv_frame_flush
	-- right after this hook returns.
	pdk.cv_frame_configure(cv, 0, pdk.new("rv_color", { 20, 24, 40 }))

	local primitive = pdk.new("rv_primitive") -- ffi.new zero-inits every field
	primitive.type = pdk.PRIMITIVE_POLYGON
	primitive.depth = 0

	local polygon = primitive.data.polygon
	polygon.fill_mode = pdk.PRIMITIVE_FILL_MODE_FLAT_COLOURED
	polygon.addr_texture = 0
	polygon.addr_palette = 0
	polygon.mapping = pdk.TEXWRAP_CLAMP -- unused by a flat-coloured fill, but not optional
	polygon.vertex_count = 3

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

	-- The sprite next to the triangle: SAMPLE_TEXTURE against the residency
	-- disc_initialize (or, after a reload, a since-run M.asset_changed)
	-- acquired. Asked fresh every frame, not cached: an address is only
	-- valid until that texture is reloaded. Guarded by tex_res because
	-- acquire can fail (e.g. the asset missing) without disc_initialize
	-- itself refusing to start.
	if state.tex_res then
		local cd = pdk.pdko_cd(pdk.cast("rv_pdko*", o_))
		local sprite_primitive = pdk.new("rv_primitive")
		sprite_primitive.type = pdk.PRIMITIVE_SPRITE
		sprite_primitive.depth = 1

		local sprite = sprite_primitive.data.sprite
		sprite.fill_mode = pdk.PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE
		sprite.addr_texture = tonumber(pdk.cd_texture_addr(cd, state.tex_res))
		sprite.addr_palette = tonumber(pdk.cd_texture_palette_addr(cd, state.tex_res))
		sprite.color.r, sprite.color.g, sprite.color.b = 255, 255, 255
		sprite.mapping = pdk.TEXWRAP_CLAMP
		sprite.x = 16
		sprite.y = 16
		sprite.width = tonumber(pdk.cd_texture_width(cd, state.tex_res))
		sprite.height = tonumber(pdk.cd_texture_height(cd, state.tex_res))

		pdk.cv_frame_put(cv, sprite_primitive)
	end
end

function M.disc_shutdown(o_) end

return M
