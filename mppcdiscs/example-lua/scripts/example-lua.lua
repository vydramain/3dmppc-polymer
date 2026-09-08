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

-- The controllers, cast to their real pointer types once disc_initialize
-- hands them over (see below) - every later hook reaches hardware through
-- these, the same way example-cpp.cpp re-derives its rv_cv* each frame.
local cv, ca, cio

-- screen_width/screen_height: read back once in disc_initialize (see there)
-- and reused every frame_render, instead of asking the console again per
-- frame for a number that cannot change mid-disc.
local screen_width, screen_height

-- Printed while the CHUNK BODY runs, i.e. already during rv_cl_script_entry's
-- first raise - before disc_initialize or any other hook is ever called. This
-- is the first milestone: seeing this line proves the bytecode actually
-- executes.
print("Hello from example lua!")

function M.disc_initialize(cv_, ca_, cio_)
	-- cv_/ca_/cio_ arrive as light userdata (rv_cl_stack_push_pointer on the
	-- C++ side) - pdk.cast (== ffi.cast, see rv_pccl.cpp) is what turns each
	-- back into its real controller pointer type. This script never says
	-- `ffi` itself: pdk is the only vocabulary it uses.
	cv = pdk.cast("rv_cv*", cv_)
	ca = pdk.cast("rv_ca*", ca_)
	cio = pdk.cast("rv_cio*", cio_)

	-- Read something real back through pdk and print it: the headless-
	-- verifiable proof that a Lua call reached the console's own
	-- rv_cv_screen_width and got its real answer, not a stub.
	screen_width = tonumber(pdk.cv_screen_width(cv))
	screen_height = tonumber(pdk.cv_screen_height(cv))
	print(string.format("example-lua: screen is %dx%d (read through pdk)", screen_width, screen_height))
end

function M.frame_update(dt)
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

	set_vertex(0, screen_width / 2, 40, 255, 80, 80)
	set_vertex(1, 40, screen_height - 40, 80, 255, 80)
	set_vertex(2, screen_width - 40, screen_height - 40, 80, 80, 255)

	pdk.cv_frame_put(cv, primitive)
end

function M.disc_shutdown() end

return M
