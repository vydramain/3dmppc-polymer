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

-- Declares the shape of `state` (below). The console checks a reload
-- candidate's live state table against this BEFORE attach() runs: a missing
-- declared key is inserted, a stored key not declared here or with a
-- mismatched type refuses the reload.
M.state_shape = {
	frame_count = 0,
	screen_width = 0,
	screen_height = 0,
}

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

-- The one texture this script draws. It never acquires or releases it - it
-- only ever names it. The drive makes it resident the first time any of the
-- four pdk.cd_texture_* calls asks for this name, keeps it resident and
-- refreshes it in place on a dev reload, and frees it itself when this disc
-- unloads, so this script has nothing to hold onto but the name.
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
	-- The console has already checked s against M.state_shape (above) -
	-- structure and types are settled by the time this runs. What is left is
	-- the SEMANTIC half a shape cannot express: attach still has the right to
	-- refuse a structurally valid state it judges unusable.
	state = s
	return true
end

function M.disc_initialize(o_)
	local cv = pdk.cv(o_)

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
	local cv = pdk.cv(o_)

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

	-- The sprite next to the triangle: SAMPLE_TEXTURE against ASSET_TEXTURE_NAME,
	-- asked for by name. The drive resolves the name to a resident texture on
	-- the first ask (or on every ask, once it already is one) and hands back
	-- its current address - asked fresh every frame, not cached, because an
	-- address is only valid until that texture is reloaded. Guarded by the
	-- address because the name can fail to resolve (e.g. the asset missing)
	-- without disc_initialize itself refusing to start.
	local cd = pdk.cd(o_)
	local addr_texture = tonumber(pdk.cd_texture_addr(cd, ASSET_TEXTURE_NAME))
	if addr_texture >= 0 then
		local sprite_primitive = pdk.new("rv_primitive")
		sprite_primitive.type = pdk.PRIMITIVE_SPRITE
		sprite_primitive.depth = 1

		local sprite = sprite_primitive.data.sprite
		sprite.fill_mode = pdk.PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE
		sprite.addr_texture = addr_texture
		sprite.addr_palette = tonumber(pdk.cd_texture_palette_addr(cd, ASSET_TEXTURE_NAME))
		sprite.color.r, sprite.color.g, sprite.color.b = 255, 255, 255
		sprite.mapping = pdk.TEXWRAP_CLAMP
		sprite.x = 16
		sprite.y = 16
		sprite.width = tonumber(pdk.cd_texture_width(cd, ASSET_TEXTURE_NAME))
		sprite.height = tonumber(pdk.cd_texture_height(cd, ASSET_TEXTURE_NAME))

		pdk.cv_frame_put(cv, sprite_primitive)
	end
end

function M.disc_shutdown(o_) end

return M
