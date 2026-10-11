#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdklib
{

// --- the .mppcmod container ---
//
// One baked model: 16-byte header, parts, vertices, triangles. Little-endian.
// Coordinates are glTF's (right-handed, Y up, metres); nothing converts them.
//
// This header is the ONE description of that layout: mppcmodbaker will write
// it, mppcburner will check it, the console will load it. The header is 16
// bytes, so its fields are aligned; the rest is not padded, because the reader
// decodes each field from bytes instead of casting pointers into the file.

/// Bytes of the fixed header.
inline constexpr int rv_mppcmod_header_size = 16;

/// Container version: the PDK version, packed. Readers accept it or an older
/// same-major minor (rv_version_compatible).
inline constexpr uint16_t rv_mppcmod_version = rv_version_packed16;

/// Bits in one byte; the version packs major above it.
inline constexpr int rv_mppcmod_byte_bits = 8;
/// Low byte of a packed version: the minor.
inline constexpr uint32_t rv_mppcmod_minor_mask = 0xFF;

/// Byte offsets inside the header.
enum rv_mppcmod_offset : int {
    RV_MPPCMOD_OFF_MAGIC = 0,        ///< 4 bytes, "MPMD"
    RV_MPPCMOD_OFF_VERSION = 4,      ///< uint16, rv_mppcmod_version
    RV_MPPCMOD_OFF_PART_COUNT = 6,   ///< uint16
    RV_MPPCMOD_OFF_VERTEX_COUNT = 8, ///< uint32
    RV_MPPCMOD_OFF_TRI_COUNT = 12    ///< uint32
};

/// The four magic bytes a .mppcmod opens with.
inline constexpr char rv_mppcmod_magic[4] = { 'M', 'P', 'M', 'D' };

/// Offsets inside one part record; the name bytes follow the fixed part.
enum rv_mppcmod_part_offset : int {
    RV_MPPCMOD_PART_OFF_FIRST_TRI = 0, ///< uint32, first triangle
    RV_MPPCMOD_PART_OFF_TRI_COUNT = 4, ///< uint32
    RV_MPPCMOD_PART_OFF_COLOR = 8,     ///< 3 x uint8, RGB
    RV_MPPCMOD_PART_OFF_NAME_LEN = 11, ///< uint8, name bytes, no NUL
    RV_MPPCMOD_PART_FIXED_BYTES = 12   ///< size of everything before the name
};

/// Offsets inside one vertex record.
enum rv_mppcmod_vertex_offset : int {
    RV_MPPCMOD_VERT_OFF_POS = 0,    ///< 3 x float32
    RV_MPPCMOD_VERT_OFF_UV = 12,    ///< 2 x float32
    RV_MPPCMOD_VERT_OFF_COLOR = 20, ///< 3 x uint8, RGB
    RV_MPPCMOD_VERT_BYTES = 24      ///< includes one pad byte, so records stay 4-byte aligned
};

/// Bytes of one triangle: 3 x u16 vertex indices.
inline constexpr int rv_mppcmod_triangle_bytes = 6;
/// Vertices are addressed by u16 indices. The format stops at 65535 vertices
/// (largest index 0xFFFF), so the count fits a u16 with no off-by-one; u16
/// alone could address 65536.
inline constexpr uint32_t rv_mppcmod_max_vertices = 0xFFFF;
/// The part count is u16.
inline constexpr uint32_t rv_mppcmod_max_parts = 0xFFFF;
/// The name length is u8; zero means a colour-only part.
inline constexpr size_t rv_mppcmod_max_name = 0xFF;
/// Bytes below this are control codes (NUL included) and may not appear in a name.
inline constexpr unsigned char rv_mppcmod_first_printable = 0x20;
/// DEL is a control code too; it may not appear in a name.
inline constexpr unsigned char rv_mppcmod_delete_byte = 0x7F;

static_assert(sizeof(float) == 4, "the container stores float32 positions and uvs");
// write() emits a vertex's fields in the order the offsets name; this keeps them equal.
static_assert(RV_MPPCMOD_VERT_OFF_UV == 3 * sizeof(float) && RV_MPPCMOD_VERT_OFF_COLOR == 5 * sizeof(float),
    "vertex offsets must follow the 3 position floats and 2 uv floats in order");

/// A run of triangles drawn with one base colour and at most one texture.
struct rv_mppcmod_part {
    uint32_t first_tri = 0;
    uint32_t tri_count = 0;
    std::array<uint8_t, 3> color = {};
    std::string texture; ///< disc resource name, e.g. "board-0.mppctex"; empty = colour only
};

struct rv_mppcmod_vertex {
    std::array<float, 3> pos = {};
    std::array<float, 2> uv = {}; ///< glTF: (0,0) is the top-left texel
    std::array<uint8_t, 3> color = {};
};

struct rv_mppcmod_triangle {
    std::array<uint16_t, 3> index = {};
};

/// One model, already decoded.
struct rv_mppcmod {
    std::vector<rv_mppcmod_part> parts;
    std::vector<rv_mppcmod_vertex> vertices;
    std::vector<rv_mppcmod_triangle> triangles;
};

namespace detail
{
inline uint32_t rv_mppcmod_read_le(const std::byte *p, size_t bytes)
{
    uint32_t v = 0;
    for (size_t i = 0; i < bytes; ++i) {
        v |= static_cast<uint32_t>(std::to_integer<uint8_t>(p[i])) << (rv_mppcmod_byte_bits * i);
    }
    return v;
}

inline void rv_mppcmod_put_le(std::vector<uint8_t> *out, uint32_t v, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) {
        out->push_back(static_cast<uint8_t>(v >> (rv_mppcmod_byte_bits * i)));
    }
}

// A texture name is a disc resource name: no path or drive separator, no control
// bytes, not "." or "..".
inline bool rv_mppcmod_name_is_plain(const std::string &name)
{
    if (name == "." || name == "..") {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char c) {
        const auto uc = static_cast<unsigned char>(c);
        return c == '/' || c == '\\' || c == ':' || uc < rv_mppcmod_first_printable || uc == rv_mppcmod_delete_byte;
    });
}
} // namespace detail

// --- the ONE validate, the ONE parse, the ONE write ---
//
// Every format rule lives in rv_mppcmod_validate(). parse() calls it after
// decoding and write() calls it before encoding, so a model write() refuses is
// one parse() refuses too.

/// Checks every rule a decoded model must hold. Returns RV_OK, or RV_ERR_INVAL
/// with `error` set to one sentence naming the broken rule.
inline int rv_mppcmod_validate(const rv_mppcmod &model, std::string &error)
{
    if (model.parts.size() > rv_mppcmod_max_parts) {
        error = "has " + std::to_string(model.parts.size()) + " parts, more than the 65535 a u16 count holds";
        return RV_ERR_INVAL;
    }
    if (model.vertices.size() > rv_mppcmod_max_vertices) {
        error = "has " + std::to_string(model.vertices.size()) + " vertices, more than the 65535 the format allows";
        return RV_ERR_INVAL;
    }
    if (model.triangles.size() > std::numeric_limits<uint32_t>::max()) {
        error = "has " + std::to_string(model.triangles.size()) + " triangles, more than a u32 count holds";
        return RV_ERR_INVAL;
    }
    for (size_t t = 0; t < model.triangles.size(); ++t) {
        for (uint16_t index : model.triangles[t].index) {
            if (index >= model.vertices.size()) {
                error = "triangle " + std::to_string(t) + " uses vertex " + std::to_string(index) + ", not one of the " +
                    std::to_string(model.vertices.size());
                return RV_ERR_INVAL;
            }
        }
    }

    // NaN and inf would reach the projection and the uv mapping unchecked: refused here, so
    // every reader and writer of the format refuses them alike.
    for (size_t v = 0; v < model.vertices.size(); ++v) {
        const rv_mppcmod_vertex &vertex = model.vertices[v];
        const auto is_finite = [](float x) {
            return std::isfinite(x);
        };
        const bool finite = std::all_of(vertex.pos.begin(), vertex.pos.end(), is_finite) &&
            std::all_of(vertex.uv.begin(), vertex.uv.end(), is_finite);
        if (!finite) {
            error = "vertex " + std::to_string(v) + " has a position or uv that is not a finite number";
            return RV_ERR_INVAL;
        }
    }

    // Parts as [first, end). Sorted by start, a part overlaps an earlier one
    // exactly when it starts before the previous part ends.
    std::vector<std::pair<uint64_t, uint64_t>> spans;
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const rv_mppcmod_part &part = model.parts[i];
        const uint64_t end = static_cast<uint64_t>(part.first_tri) + part.tri_count;
        if (end > model.triangles.size()) {
            error = "part " + std::to_string(i) + " ends at triangle " + std::to_string(end) + ", past the " +
                std::to_string(model.triangles.size());
            return RV_ERR_INVAL;
        }
        if (part.texture.size() > rv_mppcmod_max_name) {
            error = "part " + std::to_string(i) + " names a texture of " + std::to_string(part.texture.size()) +
                " bytes, more than 255";
            return RV_ERR_INVAL;
        }
        if (!detail::rv_mppcmod_name_is_plain(part.texture)) {
            error = "part " + std::to_string(i) +
                " names a texture that is not a plain file name (no '/', '\\', ':', control bytes, '.' or '..')";
            return RV_ERR_INVAL;
        }
        spans.emplace_back(part.first_tri, end);
    }
    std::sort(spans.begin(), spans.end());
    for (size_t k = 1; k < spans.size(); ++k) {
        if (spans[k].first < spans[k - 1].second) {
            error = "parts overlap at triangle " + std::to_string(spans[k].first);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

/// Decodes and validates one .mppcmod. `bytes` must be exactly the file: a
/// missing or extra byte fails. On failure returns RV_ERR_INVAL, leaves `out`
/// unspecified, and sets `error` to one sentence naming the broken rule.
inline int rv_mppcmod_parse(std::span<const std::byte> bytes, rv_mppcmod &out, std::string &error)
{
    const size_t size = bytes.size();
    if (size < static_cast<size_t>(rv_mppcmod_header_size)) {
        error = "is shorter than its own header";
        return RV_ERR_INVAL;
    }

    const std::byte *raw = bytes.data();
    if (std::memcmp(raw + RV_MPPCMOD_OFF_MAGIC, rv_mppcmod_magic, sizeof(rv_mppcmod_magic)) != 0) {
        error = "has no MPMD magic";
        return RV_ERR_INVAL;
    }

    const uint32_t version = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_VERSION, sizeof(uint16_t));
    const uint32_t version_major = version >> rv_mppcmod_byte_bits;
    const uint32_t version_minor = version & rv_mppcmod_minor_mask;
    if (!rv_version_compatible(version_major, version_minor)) {
        error = "is container version " + std::to_string(version_major) + "." + std::to_string(version_minor) +
            ", not compatible with the version " + rv_version_str + " this code reads";
        return RV_ERR_INVAL;
    }

    const uint32_t part_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_PART_COUNT, sizeof(uint16_t));
    const uint64_t vertex_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_VERTEX_COUNT, sizeof(uint32_t));
    const uint64_t tri_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_TRI_COUNT, sizeof(uint32_t));

    // Parts are bounds-checked before each byte is taken. Vertex and triangle
    // counts are only used to size the vectors after the total matches the file,
    // so a forged count cannot make the reader allocate more than the file holds.
    size_t pos = rv_mppcmod_header_size;
    out.parts.clear();
    for (uint32_t i = 0; i < part_count; ++i) {
        if (size - pos < static_cast<size_t>(RV_MPPCMOD_PART_FIXED_BYTES)) {
            error = "ends inside part " + std::to_string(i);
            return RV_ERR_INVAL;
        }
        const std::byte *rec = raw + pos;
        rv_mppcmod_part part;
        part.first_tri = detail::rv_mppcmod_read_le(rec + RV_MPPCMOD_PART_OFF_FIRST_TRI, sizeof(uint32_t));
        part.tri_count = detail::rv_mppcmod_read_le(rec + RV_MPPCMOD_PART_OFF_TRI_COUNT, sizeof(uint32_t));
        for (size_t c = 0; c < part.color.size(); ++c) {
            part.color[c] = std::to_integer<uint8_t>(rec[RV_MPPCMOD_PART_OFF_COLOR + c]);
        }
        const size_t name_len = std::to_integer<uint8_t>(rec[RV_MPPCMOD_PART_OFF_NAME_LEN]);
        pos += RV_MPPCMOD_PART_FIXED_BYTES;
        if (size - pos < name_len) {
            error = "ends inside the name of part " + std::to_string(i);
            return RV_ERR_INVAL;
        }
        part.texture.assign(reinterpret_cast<const char *>(raw + pos), name_len);
        pos += name_len;
        out.parts.push_back(std::move(part));
    }

    const uint64_t need = pos + vertex_count * RV_MPPCMOD_VERT_BYTES + tri_count * rv_mppcmod_triangle_bytes;
    if (need != size) {
        error = "is " + std::to_string(size) + " bytes, but its parts, vertices and triangles need " + std::to_string(need);
        return RV_ERR_INVAL;
    }

    out.vertices.assign(vertex_count, {});
    for (rv_mppcmod_vertex &vertex : out.vertices) {
        const std::byte *rec = raw + pos;
        for (size_t k = 0; k < vertex.pos.size(); ++k) {
            const uint32_t bits = detail::rv_mppcmod_read_le(rec + RV_MPPCMOD_VERT_OFF_POS + sizeof(float) * k, sizeof(float));
            vertex.pos[k] = std::bit_cast<float>(bits);
        }
        for (size_t k = 0; k < vertex.uv.size(); ++k) {
            const uint32_t bits = detail::rv_mppcmod_read_le(rec + RV_MPPCMOD_VERT_OFF_UV + sizeof(float) * k, sizeof(float));
            vertex.uv[k] = std::bit_cast<float>(bits);
        }
        for (size_t c = 0; c < vertex.color.size(); ++c) {
            vertex.color[c] = std::to_integer<uint8_t>(raw[pos + RV_MPPCMOD_VERT_OFF_COLOR + c]);
        }
        pos += RV_MPPCMOD_VERT_BYTES;
    }

    out.triangles.assign(tri_count, {});
    for (rv_mppcmod_triangle &tri : out.triangles) {
        for (size_t k = 0; k < tri.index.size(); ++k) {
            const uint32_t index = detail::rv_mppcmod_read_le(raw + pos + sizeof(uint16_t) * k, sizeof(uint16_t));
            tri.index[k] = static_cast<uint16_t>(index);
        }
        pos += rv_mppcmod_triangle_bytes;
    }

    const int checked = rv_mppcmod_validate(out, error);
    if (checked != RV_OK) {
        return checked;
    }
    return RV_OK;
}

/// Appends the .mppcmod encoding of `model` to `*out`. A model the format cannot
/// hold fails with RV_ERR_INVAL and `error` set, and `*out` is left untouched.
inline int rv_mppcmod_write(const rv_mppcmod &model, std::vector<uint8_t> *out, std::string &error)
{
    const int checked = rv_mppcmod_validate(model, error);
    if (checked != RV_OK) {
        return checked;
    }

    out->insert(out->end(), rv_mppcmod_magic, rv_mppcmod_magic + sizeof(rv_mppcmod_magic));
    detail::rv_mppcmod_put_le(out, rv_mppcmod_version, sizeof(uint16_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.parts.size()), sizeof(uint16_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.vertices.size()), sizeof(uint32_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.triangles.size()), sizeof(uint32_t));

    for (const rv_mppcmod_part &part : model.parts) {
        detail::rv_mppcmod_put_le(out, part.first_tri, sizeof(uint32_t));
        detail::rv_mppcmod_put_le(out, part.tri_count, sizeof(uint32_t));
        out->insert(out->end(), part.color.begin(), part.color.end());
        out->push_back(static_cast<uint8_t>(part.texture.size()));
        out->insert(out->end(), part.texture.begin(), part.texture.end());
    }

    for (const rv_mppcmod_vertex &vertex : model.vertices) {
        for (float f : vertex.pos) {
            detail::rv_mppcmod_put_le(out, std::bit_cast<uint32_t>(f), sizeof(float));
        }
        for (float f : vertex.uv) {
            detail::rv_mppcmod_put_le(out, std::bit_cast<uint32_t>(f), sizeof(float));
        }
        out->insert(out->end(), vertex.color.begin(), vertex.color.end());
        out->push_back(0); // pad to RV_MPPCMOD_VERT_BYTES
    }

    for (const rv_mppcmod_triangle &tri : model.triangles) {
        for (uint16_t index : tri.index) {
            detail::rv_mppcmod_put_le(out, index, sizeof(uint16_t));
        }
    }
    return RV_OK;
}

} // namespace rv_pdklib
