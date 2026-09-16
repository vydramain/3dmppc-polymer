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

-- Recover cv/ca/cio as real controller pointers from whatever `state` holds
-- right now. Called from both disc_initialize (the raw light userdata just
-- arrived) and attach (a reload may run with no disc_initialize in between -
-- see there) - one place casts, so both callers agree on the result.
--
-- This cannot be done ONCE inside attach() and cached in a local: a local
-- dies with the chunk that set it, and the whole point of this helper is to
-- rebuild working pointers AFTER a reload, when the old chunk's locals are
-- already gone and only `state`'s raw userdata is still there.
local function recast_controllers(s)
	if s.cv_raw then
		s.cv = pdk.cast("rv_cv*", s.cv_raw)
	end
	if s.ca_raw then
		s.ca = pdk.cast("rv_ca*", s.ca_raw)
	end
	if s.cio_raw then
		s.cio = pdk.cast("rv_cio*", s.cio_raw)
	end
	if s.cd_raw then
		s.cd = pdk.cast("rv_cd*", s.cd_raw)
	end
end

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
	recast_controllers(state)
	return true
end

function M.disc_initialize(cv_, ca_, cio_, cd_)
	-- cv_/ca_/cio_/cd_ arrive as light userdata (rv_cl_stack_push_pointer on
	-- the C++ side). They are stored RAW in state, not just cast to a local:
	-- a reloaded chunk's attach() has no disc_initialize call to wait for, so
	-- recast_controllers() is what turns raw userdata back into a usable
	-- pointer on EVERY attach, using whichever raw value is already in state.
	state.cv_raw = cv_
	state.ca_raw = ca_
	state.cio_raw = cio_
	state.cd_raw = cd_
	recast_controllers(state)

	-- Read something real back through pdk and log it: the headless-
	-- verifiable proof that a Lua call reached the console's own
	-- rv_cv_screen_width and got its real answer, not a stub. Stored in
	-- state, not a local, because frame_render (below) needs it and a local
	-- set here would not survive a reload.
	state.screen_width = tonumber(pdk.cv_screen_width(state.cv))
	state.screen_height = tonumber(pdk.cv_screen_height(state.cv))
	print(string.format("example-lua: screen is %dx%d (read through pdk)", state.screen_width, state.screen_height))
end

function M.frame_update(dt)
	-- The frame counter: the one field this example exists to demonstrate.
	-- It lives in `state`, so a code reload (M.attach runs, this chunk's
	-- locals do not) leaves it exactly where it was - the count CONTINUES
	-- instead of restarting at 0.
	state.frame_count = state.frame_count + 1

	-- Should the disc stop? cio is cast and reachable now, but this example
	-- does not wire a button up to it - false every frame is still the
	-- honest answer for a script that raises no stop condition of its own.
	return false
end

function M.frame_render()
	-- Same shape as example-cpp.cpp's own frame_render: configure the frame
	-- (clear colour), fill it with a primitive, and leave the flush to the
	-- disc's C++ side (src/example-lua.cpp), which calls rv_cv_frame_flush
	-- right after this hook returns.
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 20, 24, 40 }))

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

	pdk.cv_frame_put(state.cv, primitive)
end

function M.disc_shutdown() end

return M
