#pragma once

#include <cstdint>

#include "pdk/cv/rv_texel.hpp"
#include "pdk/cv/rv_vertex.hpp"

namespace rv_pdklib
{

// --- the 16-bit texel, packed and taken apart ---------------------------------
//
// One bit layout, written once. rv_texture.hpp mandates bits 0-4 red, 5-9 green,
// 10-14 blue and bit 15 STP. Before this header those three shifts were restated
// by mppcbaker, by the text renderer and twice by the rasterizer — four chances
// to disagree about a format all of them must read identically.
//
// STP is left clear by everything here: the semi-transparency modes are deferred
// on the console side, and a stored STP bit would silently change the look on the
// day they land.
//
// The QUANTISER is deliberately not folded into the packer. Eight bits become
// five in more than one legitimate way — round to nearest for a flat conversion,
// truncate after a dither threshold for a shaded pixel — and those are different
// pictures, not different spellings of one picture. So the layout is shared and
// the choice of quantiser stays with the caller.

/// Pack a 5-bit colour into the value a framebuffer or a palette entry stores.
inline constexpr uint16_t rv_texel_pack(rv_pdk::rv_color5 c)
{
    return static_cast<uint16_t>((c.r & 0x1F) | ((c.g & 0x1F) << 5) | ((c.b & 0x1F) << 10));
}

/// Take one apart again — the inverse of rv_texel_pack. STP is dropped.
inline constexpr rv_pdk::rv_color5 rv_texel_unpack(uint16_t value)
{
    return rv_pdk::rv_color5{ static_cast<uint8_t>(value & 0x1F),
        static_cast<uint8_t>((value >> 5) & 0x1F),
        static_cast<uint8_t>((value >> 10) & 0x1F) };
}

/// One channel, 8-bit to 5-bit, rounded to the NEAREST representable level.
///
/// (v * 31 + 127) / 255 maps 0 to 0 and 255 to 31. A plain `>> 3` reaches those
/// two endpoints as well but darkens everything between them, because it always
/// rounds down.
inline constexpr uint8_t rv_texel_channel5(uint8_t value)
{
    return static_cast<uint8_t>((static_cast<int>(value) * 31 + 127) / 255);
}

/// A whole colour, rounded to the nearest representable one.
inline constexpr rv_pdk::rv_color5 rv_texel_quantize(rv_pdk::rv_color c)
{
    return rv_pdk::rv_color5{ rv_texel_channel5(c.r), rv_texel_channel5(c.g),
        rv_texel_channel5(c.b) };
}

/// A whole colour, by dropping the low three bits of each channel.
///
/// The cheap conversion, and the correct one when the caller has ALREADY added a
/// dither threshold: truncation is what that threshold was chosen against, so
/// rounding on top of it would cancel half the dither.
inline constexpr rv_pdk::rv_color5 rv_texel_truncate(rv_pdk::rv_color c)
{
    return rv_pdk::rv_color5{ static_cast<uint8_t>(c.r >> 3), static_cast<uint8_t>(c.g >> 3),
        static_cast<uint8_t>(c.b >> 3) };
}

/// The darkest representable red — where an opaque black has to go.
inline constexpr uint16_t rv_texel_near_black = 0x0001;

/// Keep an opaque colour opaque.
///
/// A colour that packs to 0000h is not black on this machine, it is a HOLE
/// (pdk/cv/rv_texel.hpp). Anything that meant to be drawn must therefore be
/// nudged off that value, and 0001h is the nearest place to put it: one level of
/// red, below the console's own dithering noise floor and indistinguishable from
/// black on screen.
///
/// Whoever WRITES texels owes this call. The console never learns a substitution
/// happened — it sees a texel that is simply not a hole.
inline constexpr uint16_t rv_texel_opaque(uint16_t value)
{
    return value == rv_pdk::RV_TEXEL_TRANSPARENT ? rv_texel_near_black : value;
}

} // namespace rv_pdklib
