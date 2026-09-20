#pragma once

// The lua half of pdklib's disc-author toolbox. RV_MPPC_LUA_DISC_DEF
// (rv_dscript.hpp, beside this file) is the C++ half a disc's own .cpp
// writes to forward its hooks into a Lua chunk; this is what the SCRIPT
// SIDE of that same disc gets for free once the chunk is running. Both
// halves answer the one question pdklib exists to answer for a disc
// author - what must a primitive be filled with before the console will
// accept it, and how does a resource's name become an address - and never
// the question of what to draw with either: geometry, colour, position,
// depth and the decision to draw at all stay the game's.
//
// The console (src/rv_pconsole/cl/rv_pccl_luajit.cpp) RAISES this source
// the same way it raises its own RV_PCCL_PDK_BOOTSTRAP_SRC - one
// luaL_loadbuffer, one protected pcall, run right after that first chunk
// has finished building the `pdk` table these helpers extend - but the
// console does not AUTHOR it. Every helper below belongs to pdklib, the
// same way RV_MPPC_LUA_DISC_DEF's macro belongs to pdklib even though the
// console is what runs the disc it defines; the console only ever lends
// the Lua machine that already exists for the disc's own entry chunk.
namespace rv_pdklib
{

// `pdk` arrives as this chunk's one vararg, already carrying cast/new, the
// lazy ffi.C accessors and the per-controller `pdk.cv(o)`-style helpers
// RV_PCCL_PDK_BOOTSTRAP_SRC installed a moment earlier - this chunk only
// ADDS to that table, it never rebuilds it.
constexpr const char *RV_PDKLIB_LUA_HELPERS_SRC = R"lua(
local pdk = ...

-- A disc's resources are asked for by kind + name, four separate calls
-- (address, palette address, width, height) - see rv_cd_resource_addr's own
-- contract comment in pdk/include/pdk/cd/rv_cd.h. The kind is not the game's
-- choice: today there is exactly one, RV_CD_RESOURCE_TEXTURE, so a script
-- naming it four times over is pure repetition, not a decision - which is
-- why this helper hardcodes it and takes only the organizer and a name.
-- Returns nil rather than a table of negative addresses when the name does
-- not resolve, because that is the shape a game's own `if` already knows how
-- to test, and because a missing asset is exactly the kind of content
-- problem rv_cd_resource_addr's RV_ERR_NOENT describes, not a coding error
-- worth raising as one.
function pdk.resource_resolve(o, name)
    local cd = pdk.cd(o)
    local addr_texture = tonumber(pdk.cd_resource_addr(cd, pdk.CD_RESOURCE_TEXTURE, name))
    if addr_texture < 0 then
        return nil
    end
    return {
        addr_texture = addr_texture,
        addr_palette = tonumber(pdk.cd_resource_palette_addr(cd, pdk.CD_RESOURCE_TEXTURE, name)),
        width = tonumber(pdk.cd_resource_width(cd, pdk.CD_RESOURCE_TEXTURE, name)),
        height = tonumber(pdk.cd_resource_height(cd, pdk.CD_RESOURCE_TEXTURE, name)),
    }
end

-- Every field rv_cv_frame_put REQUIRES from a textured sprite before it will
-- accept the primitive at all: which union member it is (type), how it is
-- filled (fill_mode), how sampling behaves past its edges (mapping), and the
-- resolved texture's own address pair - none of those are choices a game
-- makes differently disc to disc, they are what "this is a sprite" MEANS to
-- the console, so they belong here rather than being retyped in every
-- script. Width/height default to the texture's own pixel size because
-- "drawn at native size" is what a disc wants until it asks for otherwise,
-- and defaulting it here still leaves the game free to overwrite it.
-- Position (x, y), depth and colour are left at ffi.new's zero-init default
-- ON PURPOSE: only the game knows where a sprite belongs on screen, whether
-- it sits in front of or behind something else, and what tint it wears, and
-- setting any of those here would be pdklib deciding game content instead of
-- console plumbing.
function pdk.primitive_sprite(resolved)
    local primitive = pdk.new("rv_primitive")
    primitive.type = pdk.PRIMITIVE_SPRITE

    local sprite = primitive.data.sprite
    sprite.fill_mode = pdk.PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE
    sprite.mapping = pdk.TEXWRAP_CLAMP
    sprite.addr_texture = resolved.addr_texture
    sprite.addr_palette = resolved.addr_palette
    sprite.width = resolved.width
    sprite.height = resolved.height
    return primitive
end

-- The flat-coloured counterpart of pdk.primitive_sprite above, for the same
-- reason: a polygon with no texture to sample still has to carry an
-- addr_texture/addr_palette and a mapping (the field is not optional, even
-- though a flat fill never reads it), and vertex_count is the one number
-- the console needs before it will look at the vertex array at all. Zeroed
-- rather than left at whatever ffi.new already zero-initialised them to, so
-- the zero is a stated fact of a flat fill rather than an accident of
-- allocation. The vertices themselves - their positions and their colours -
-- are exactly what this primitive exists to draw, so pdklib does not touch
-- them: that is the game's content, never its plumbing, and vertex_count is
-- taken as a parameter rather than inferred so this helper never has to
-- guess it from data it is not given.
function pdk.primitive_polygon(vertex_count)
    local primitive = pdk.new("rv_primitive")
    primitive.type = pdk.PRIMITIVE_POLYGON

    local polygon = primitive.data.polygon
    polygon.fill_mode = pdk.PRIMITIVE_FILL_MODE_FLAT_COLOURED
    polygon.mapping = pdk.TEXWRAP_CLAMP
    polygon.addr_texture = 0
    polygon.addr_palette = 0
    polygon.vertex_count = vertex_count
    return primitive
end
)lua";

} // namespace rv_pdklib
