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

-- The one asset this script OWNS: it uploads it once, in disc_initialize,
-- into an address it remembers, and it is the only asset name
-- M.asset_changed (below) will accept. "Owns" is the whole division of
-- labour behind asset reload - the console can re-read bytes off the drive
-- but has no idea which video address a game's own code put them at, so the
-- game is the only side that can put a changed texture back.
local ASSET_TEXTURE_NAME = "example-sprite.mppctex"

-- The .mppctex container layout (pdk/lib/include/pdklib/rv_textures/rv_mppctex.hpp):
-- a 16-byte header, then the palette (if the format has one), then the
-- texels. Restated here in Lua rather than shared with that C++ header,
-- exactly the way example-cpp.cpp restates it as its own
-- rv_example_cpp_texheader instead of including it either.
local MPPCTEX_HEADER_SIZE = 16
local MPPCTEX_PALETTE_ENTRY_BYTES = 2

local function read_u16(buf, off)
	return buf[off] + buf[off + 1] * 256
end

-- Bytes the texels of `header` occupy, palette excluded - mirrors
-- rv_mppctex_texel_bytes. IDX4 packs two texels per byte and pads each ROW.
local function mppctex_texel_bytes(header)
	if header.format == pdk.TEXFMT_IDX4 then
		return math.floor((header.width + 1) / 2) * header.height
	elseif header.format == pdk.TEXFMT_IDX8 then
		return header.width * header.height
	else
		return header.width * header.height * 2
	end
end

-- Parses the 16-byte header out of `buf` (an ffi uint8_t[?] of `size`
-- bytes). Returns a plain Lua table of plain Lua numbers - not ffi cdata -
-- so nothing here risks ending up stored in `state`.
local function parse_mppctex(buf, size)
	if size < MPPCTEX_HEADER_SIZE then
		return nil, "texture header truncated"
	end
	if buf[0] ~= 0x4d or buf[1] ~= 0x50 or buf[2] ~= 0x54 or buf[3] ~= 0x58 then -- "MPTX"
		return nil, "bad texture magic"
	end
	if read_u16(buf, 4) ~= 1 then
		return nil, "unsupported texture container version"
	end
	return {
		format = read_u16(buf, 6),
		width = read_u16(buf, 8),
		height = read_u16(buf, 10),
		palette_count = read_u16(buf, 12),
	}
end

-- Opens, sizes and reads one asset off the mounted drive through pdk.cd_*
-- (pdk/include/pdk/cd/rv_cd.h), the same three calls
-- example-cpp.cpp's read_asset does. Returns an ffi uint8_t[?] buffer and its
-- length, or nil plus a reason.
local function read_asset_bytes(cd, name)
	local handle = pdk.cd_asset_open(cd, name)
	if handle < 0 then
		return nil, nil, "asset '" .. name .. "' not found on the drive"
	end
	local size = tonumber(pdk.cd_asset_size(cd, handle))
	if size < 0 then
		return nil, nil, "could not size asset '" .. name .. "'"
	end
	local buf = pdk.new("uint8_t[?]", size)
	local read = tonumber(pdk.cd_asset_read(cd, handle, buf, size))
	if read < 0 then
		return nil, nil, "could not read asset '" .. name .. "'"
	end
	return buf, read
end

-- Allocates FRESH video memory for `header`'s palette (if any) and texels,
-- and writes `buf`'s bytes into it - the boot-time path, called once from
-- disc_initialize. Returns a plain table describing what it allocated, or
-- nil plus a reason; on any failure it frees whatever it already allocated,
-- so a failed upload never leaks a video allocation.
local function upload_texture(cv, buf, size, header)
	local palette_bytes = header.palette_count * MPPCTEX_PALETTE_ENTRY_BYTES
	local texel_offset = MPPCTEX_HEADER_SIZE + palette_bytes
	local texel_bytes = mppctex_texel_bytes(header)
	if size < texel_offset + texel_bytes then
		return nil, "texture payload truncated"
	end

	local pal_addr = 0
	if header.palette_count > 0 then
		pal_addr = tonumber(pdk.cv_video_asset_malloc(cv, palette_bytes))
		if pal_addr < 0 then
			return nil, "video memory exhausted (palette)"
		end
		local palette = pdk.new("rv_texture")
		palette.format = pdk.TEXFMT_DIRECT15
		palette.data = buf + MPPCTEX_HEADER_SIZE
		palette.size = palette_bytes
		palette.width = header.palette_count
		palette.height = 1
		if pdk.cv_video_asset_write(cv, pal_addr, palette) < 0 then
			pdk.cv_video_asset_free(cv, pal_addr)
			return nil, "video write failed (palette)"
		end
	end

	local tex_addr = tonumber(pdk.cv_video_asset_malloc(cv, texel_bytes))
	if tex_addr < 0 then
		if pal_addr ~= 0 then
			pdk.cv_video_asset_free(cv, pal_addr)
		end
		return nil, "video memory exhausted (texels)"
	end
	local texels = pdk.new("rv_texture")
	texels.format = header.format
	texels.data = buf + texel_offset
	texels.size = texel_bytes
	texels.width = header.width
	texels.height = header.height
	if pdk.cv_video_asset_write(cv, tex_addr, texels) < 0 then
		pdk.cv_video_asset_free(cv, tex_addr)
		if pal_addr ~= 0 then
			pdk.cv_video_asset_free(cv, pal_addr)
		end
		return nil, "video write failed (texels)"
	end

	return {
		addr = tex_addr,
		pal_addr = pal_addr,
		texel_bytes = texel_bytes,
		pal_bytes = palette_bytes,
		width = header.width,
		height = header.height,
		format = header.format,
	}
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

	-- Uploaded exactly once here, never in attach(): disc_initialize is the
	-- only hook that runs at boot and never again, and it is the video
	-- address it allocates NOW that M.asset_changed (below) has to remember
	-- across every future code reload - hence storing it in `state`, not a
	-- local.
	local buf, size, err = read_asset_bytes(cd, ASSET_TEXTURE_NAME)
	if not buf then
		print("example-lua: " .. err)
		return
	end
	local header, herr = parse_mppctex(buf, size)
	if not header then
		print("example-lua: " .. herr)
		return
	end
	local tex, uerr = upload_texture(cv, buf, size, header)
	if not tex then
		print("example-lua: " .. uerr)
		return
	end

	state.tex_name = ASSET_TEXTURE_NAME
	state.tex_addr = tex.addr
	state.tex_pal_addr = tex.pal_addr
	state.tex_texel_bytes = tex.texel_bytes
	state.tex_pal_bytes = tex.pal_bytes
	state.tex_width = tex.width
	state.tex_height = tex.height
	state.tex_format = tex.format
end

-- The console's asset-reload hook: called on the ENTRY CHUNK, at a frame
-- boundary, with the flat entry name whose bytes on the drive just changed.
-- true means this script has already re-read and re-uploaded it; false plus
-- a reason is a refusal, reported to the client as an error, and leaves the
-- old video data exactly where it was.
--
-- Only the ONE asset this script uploaded in disc_initialize (above) is
-- ever accepted - a name it does not own is refused without touching
-- anything, and a new texture that does not fit the video memory already
-- allocated for the old one is refused too, checked BEFORE a single byte is
-- written, for the same reason attach() checks compatibility before writing
-- state: a refusal must leave things exactly as they were.
function M.asset_changed(name)
	if name ~= state.tex_name then
		return false, "asset '" .. tostring(name) .. "' is not owned by this script"
	end
	if not state.tex_addr or state.tex_addr == 0 then
		return false, "no texture currently uploaded to refresh"
	end

	local o = pdk.cast("rv_pdko*", state.pdko)
	local cv = pdk.pdko_cv(o)
	local cd = pdk.pdko_cd(o)

	local buf, size, err = read_asset_bytes(cd, name)
	if not buf then
		return false, err
	end

	local header, herr = parse_mppctex(buf, size)
	if not header then
		return false, herr
	end

	local palette_bytes = header.palette_count * MPPCTEX_PALETTE_ENTRY_BYTES
	local texel_offset = MPPCTEX_HEADER_SIZE + palette_bytes
	local texel_bytes = mppctex_texel_bytes(header)
	if size < texel_offset + texel_bytes then
		return false, "texture payload truncated"
	end

	-- The video memory allocated at boot is exactly state.tex_texel_bytes /
	-- state.tex_pal_bytes wide, and this script has no way to grow it - a
	-- texture that no longer matches those sizes would be written past the
	-- block and corrupt whatever the video allocator put next to it.
	if texel_bytes ~= state.tex_texel_bytes or palette_bytes ~= state.tex_pal_bytes then
		return false, "new texture does not fit the allocated video memory"
	end

	if palette_bytes > 0 then
		local palette = pdk.new("rv_texture")
		palette.format = pdk.TEXFMT_DIRECT15
		palette.data = buf + MPPCTEX_HEADER_SIZE
		palette.size = palette_bytes
		palette.width = header.palette_count
		palette.height = 1
		if pdk.cv_video_asset_write(cv, state.tex_pal_addr, palette) < 0 then
			return false, "video write failed (palette)"
		end
	end

	local texels = pdk.new("rv_texture")
	texels.format = header.format
	texels.data = buf + texel_offset
	texels.size = texel_bytes
	texels.width = header.width
	texels.height = header.height
	if pdk.cv_video_asset_write(cv, state.tex_addr, texels) < 0 then
		return false, "video write failed (texels)"
	end

	-- width/height/format may differ even at a matching byte count (e.g. a
	-- taller-but-narrower IDX8 image) - kept in sync so frame_render draws
	-- the new shape, not the old one.
	state.tex_width = header.width
	state.tex_height = header.height
	state.tex_format = header.format
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

	-- The sprite next to the triangle: SAMPLE_TEXTURE against the address
	-- disc_initialize (or, after a reload, a since-run M.asset_changed)
	-- uploaded into. Guarded by tex_addr because upload can fail (e.g. the
	-- asset missing) without disc_initialize itself refusing to start.
	if state.tex_addr and state.tex_addr ~= 0 then
		local sprite_primitive = pdk.new("rv_primitive")
		sprite_primitive.type = pdk.PRIMITIVE_SPRITE
		sprite_primitive.depth = 1

		local sprite = sprite_primitive.data.sprite
		sprite.fill_mode = pdk.PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE
		sprite.addr_texture = state.tex_addr
		sprite.addr_palette = state.tex_pal_addr or 0
		sprite.color.r, sprite.color.g, sprite.color.b = 255, 255, 255
		sprite.mapping = pdk.TEXWRAP_CLAMP
		sprite.x = 16
		sprite.y = 16
		sprite.width = state.tex_width
		sprite.height = state.tex_height

		pdk.cv_frame_put(cv, sprite_primitive)
	end
end

function M.disc_shutdown(o_) end

return M
