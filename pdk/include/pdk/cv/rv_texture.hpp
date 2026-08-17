#pragma once

#include <cstdint>
#include <cstring> // std::strcmp, for the spelling lookup below

namespace rv_pdk
{

// How the texels of an uploaded texture are encoded. This is what decides
// whether a palette (CLUT) is needed: the indexed formats store a palette index
// per texel and must be drawn with addr_palette set; the direct format stores
// the colour in the texel itself and ignores the palette.
//
// A DIRECT15 texel — and every palette entry — is one 16-bit value:
//   0-4   red   (0..31)
//   5-9   green (0..31)
//   10-14 blue  (0..31)
//   15    semi-transparency flag (STP); stored, but takes effect only when the
//         semi-transparent blending modes land (DEFERRED)
// The value 0000h is FULLY TRANSPARENT: that pixel is not drawn at all — the
// hole that makes cut-out sprites possible. Deliberate PSX inheritance: opaque
// black cannot be stored in a texture; use a near-black such as 0001h (darkest
// red) or 0400h (darkest blue). 8000h is black with the STP flag.
//
// New formats take a new enumerator; existing values never change (they are ABI,
// like rv_err). The numeric value is the bit width of one texel, as a mnemonic.
enum rv_texfmt {
	RV_TEXFMT_IDX4 = 4,      // 4-bit palette index  (16-colour palette)
	RV_TEXFMT_IDX8 = 8,      // 8-bit palette index  (256-colour palette)
	RV_TEXFMT_DIRECT15 = 15, // 15-bit direct colour + STP bit (no palette)
};

// One enumerator and the spelling it takes in text — the name a disc.toml uses
// for [textures] format, and the name mppcbaker takes after --format. The pair
// lives here because the enumerator and its spelling are one fact: a format
// added below without a row in the table has no name anywhere in the toolchain.
struct rv_texfmt_name {
	rv_texfmt format;
	const char *text;

	// Look up a row by enumerator. Returns nullptr when `format` is not one of
	// the enumerators — a variable of an unscoped enum type may legally hold any
	// value its range covers (0..15 here), so a miss is a real possibility and
	// not an internal error.
	static const rv_texfmt_name *by_format(rv_texfmt format);

	// Look up a row by spelling. Returns nullptr on an unknown one, which is
	// what a typo in a hand-written manifest looks like from in here.
	static const rv_texfmt_name *by_text(const char *text);
};

inline constexpr rv_texfmt_name rv_texfmt_names[]{
	{ rv_texfmt::RV_TEXFMT_IDX4, "idx4" },
	{ rv_texfmt::RV_TEXFMT_IDX8, "idx8" },
	{ rv_texfmt::RV_TEXFMT_DIRECT15, "direct15" }
};

// Defined after the table because they walk it. Linear scan: three rows, and a
// sorted structure would cost more to keep honest than it could ever save.
inline const rv_texfmt_name *rv_texfmt_name::by_format(rv_texfmt format)
{
	for (const rv_texfmt_name &row : rv_texfmt_names) {
		if (row.format == format) {
			return &row;
		}
	}
	return nullptr;
}

inline const rv_texfmt_name *rv_texfmt_name::by_text(const char *text)
{
	if (text == nullptr) {
		return nullptr;
	}
	for (const rv_texfmt_name &row : rv_texfmt_names) {
		if (std::strcmp(row.text, text) == 0) {
			return &row;
		}
	}
	return nullptr;
}

// A block of pixel data the game hands to rv_cv::video_asset_write() for upload
// into video RAM. Plain POD: it only *borrows* the bytes (the game owns them)
// and carries its own size and dimensions, so the upload call needs no separate
// length argument. The bytes are copied during the call; the game may release
// its own buffer once it returns.
//
// A palette is uploaded the same way, as an array of 16-bit entries in the SAME
// layout as a DIRECT15 texel (see above): pass format = RV_TEXFMT_DIRECT15,
// width = the number of entries (16 for IDX4, 256 for IDX8), height = 1.
// Transparency is decided AFTER the palette lookup, so the 0000h / STP rules
// reach the indexed formats through their palette.
//
// The maximum texture size is IMPLEMENTATION-defined — query
// rv_cv::texture_max_width() / texture_max_height(); video_asset_write()
// enforces it (RV_ERR_INVAL). The wide fields here promise nothing about the
// limit — they just let it grow without an ABI break.
struct rv_texture {
	rv_texfmt format; // how to read `data` (and whether a palette is needed)

	const void *data; // texel bytes, owned by the game (main RAM)
	uint64_t size;    // length of `data`, in bytes
	uint64_t width;   // texel columns
	uint64_t height;  // texel rows
};

} // namespace rv_pdk
