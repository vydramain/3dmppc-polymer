#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"

namespace rv_pdklib
{

// --- the integer .mppcmod layout ---
//
// It replaces the old layout in rv_mppcmod.hpp; the next task deletes that one and this header
// becomes the ONE description. Little-endian. Vertices are int16 at scale 2^-shift metres;
// version, part and triangle records come from rv_mppcmod.hpp.

/// Bytes of the integer header: the common 16, then the shift and 3 reserved bytes.
inline constexpr int rv_mppcmod_int_header_size = 20;

/// Magic of the integer layout. Not "MPMD": the PDK version is unchanged on this branch and the
/// sizes alone cannot tell the two layouts apart, so the magic does.
inline constexpr char rv_mppcmod_int_magic[4] = { 'M', 'P', 'M', 'I' };

/// Byte offsets of the fields after the common 16-byte header.
enum rv_mppcmod_int_offset : int {
    RV_MPPCMOD_INT_OFF_SHIFT = 16,    ///< uint8, metres = v / 2^shift
    RV_MPPCMOD_INT_OFF_RESERVED = 17, ///< 3 bytes, written as 0, refused if not 0
};

/// Reserved header bytes after the shift.
inline constexpr int rv_mppcmod_int_reserved_bytes = 3;

/// Largest vertex shift: 12 gives 1/4096 m steps over the int16 range of +-8 m.
inline constexpr uint8_t rv_mppcmod_int_max_shift = 12;

/// Offsets inside one vertex record.
enum rv_mppcmod_int_vertex_offset : int {
    RV_MPPCMOD_INT_VERT_OFF_POS = 0,    ///< 3 x int16
    RV_MPPCMOD_INT_VERT_OFF_UV = 6,     ///< 2 x int16, 4.12 fixed point: 4096 is the whole texture
    RV_MPPCMOD_INT_VERT_OFF_COLOR = 10, ///< 3 x uint8, RGB
    RV_MPPCMOD_INT_VERT_BYTES = 14      ///< includes one pad byte, written as 0
};

static_assert(RV_MPPCMOD_INT_OFF_RESERVED + rv_mppcmod_int_reserved_bytes == rv_mppcmod_int_header_size,
    "the header ends after the reserved bytes");

/// One vertex in the integer layout.
struct rv_mppcmod_int_vertex {
    std::array<int16_t, 3> pos = {};
    std::array<int16_t, 2> uv = {};
    std::array<uint8_t, 3> color = {};
};

/// One model in the integer layout, already decoded.
struct rv_mppcmod_int_model {
    uint8_t shift = 0; ///< metres = v / 2^shift, at most rv_mppcmod_int_max_shift
    std::vector<rv_mppcmod_part> parts;
    std::vector<rv_mppcmod_int_vertex> vertices;
    std::vector<rv_mppcmod_triangle> triangles;
};

namespace detail
{
inline int16_t rv_mppcmod_int_read_i16(const std::byte *p)
{
    return std::bit_cast<int16_t>(static_cast<uint16_t>(rv_mppcmod_read_le(p, sizeof(uint16_t))));
}
} // namespace detail

// --- validate, parse, write: the ONE set once the old layout is deleted ---

/// Checks every rule an integer model must hold. Returns RV_OK, or RV_ERR_INVAL
/// with `error` set to one sentence naming the broken rule.
inline int rv_mppcmod_int_validate(const rv_mppcmod_int_model &model, std::string &error)
{
    if (model.shift > rv_mppcmod_int_max_shift) {
        error = "has vertex shift " + std::to_string(model.shift) + ", more than the " +
            std::to_string(rv_mppcmod_int_max_shift) + " the format allows";
        return RV_ERR_INVAL;
    }
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

/// Decodes and validates one integer .mppcmod. A file shorter than the 20-byte header fails.
/// On failure returns RV_ERR_INVAL, leaves `out` unspecified, and sets `error` to one sentence.
inline int rv_mppcmod_int_parse(std::span<const std::byte> bytes, rv_mppcmod_int_model &out, std::string &error)
{
    const size_t size = bytes.size();
    if (size < static_cast<size_t>(rv_mppcmod_int_header_size)) {
        error = "is shorter than its own header";
        return RV_ERR_INVAL;
    }

    const std::byte *raw = bytes.data();
    if (std::memcmp(raw + RV_MPPCMOD_OFF_MAGIC, rv_mppcmod_magic, sizeof(rv_mppcmod_magic)) == 0) {
        error = "is an MPMD model from an older PDK layout; bake it again";
        return RV_ERR_INVAL;
    }
    if (std::memcmp(raw + RV_MPPCMOD_OFF_MAGIC, rv_mppcmod_int_magic, sizeof(rv_mppcmod_int_magic)) != 0) {
        error = "has no MPMI magic";
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

    for (int k = 0; k < rv_mppcmod_int_reserved_bytes; ++k) {
        if (std::to_integer<uint8_t>(raw[RV_MPPCMOD_INT_OFF_RESERVED + k]) != 0) {
            error = "has non-zero reserved header bytes";
            return RV_ERR_INVAL;
        }
    }

    const uint32_t part_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_PART_COUNT, sizeof(uint16_t));
    const uint64_t vertex_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_VERTEX_COUNT, sizeof(uint32_t));
    const uint64_t tri_count = detail::rv_mppcmod_read_le(raw + RV_MPPCMOD_OFF_TRI_COUNT, sizeof(uint32_t));

    // Parts are bounds-checked before each byte is taken. Vertex and triangle
    // counts are only used to size the vectors after the total matches the file,
    // so a forged count cannot make the reader allocate more than the file holds.
    size_t pos = rv_mppcmod_int_header_size;
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

    const uint64_t need = pos + vertex_count * RV_MPPCMOD_INT_VERT_BYTES + tri_count * rv_mppcmod_triangle_bytes;
    if (need != size) {
        error = "is " + std::to_string(size) + " bytes, but its parts, vertices and triangles need " + std::to_string(need);
        return RV_ERR_INVAL;
    }

    out.shift = std::to_integer<uint8_t>(raw[RV_MPPCMOD_INT_OFF_SHIFT]);
    out.vertices.assign(vertex_count, {});
    for (rv_mppcmod_int_vertex &vertex : out.vertices) {
        const std::byte *rec = raw + pos;
        for (size_t k = 0; k < vertex.pos.size(); ++k) {
            vertex.pos[k] = detail::rv_mppcmod_int_read_i16(rec + RV_MPPCMOD_INT_VERT_OFF_POS + sizeof(int16_t) * k);
        }
        for (size_t k = 0; k < vertex.uv.size(); ++k) {
            vertex.uv[k] = detail::rv_mppcmod_int_read_i16(rec + RV_MPPCMOD_INT_VERT_OFF_UV + sizeof(int16_t) * k);
        }
        for (size_t c = 0; c < vertex.color.size(); ++c) {
            vertex.color[c] = std::to_integer<uint8_t>(rec[RV_MPPCMOD_INT_VERT_OFF_COLOR + c]);
        }
        pos += RV_MPPCMOD_INT_VERT_BYTES;
    }

    out.triangles.assign(tri_count, {});
    for (rv_mppcmod_triangle &tri : out.triangles) {
        for (size_t k = 0; k < tri.index.size(); ++k) {
            const uint32_t index = detail::rv_mppcmod_read_le(raw + pos + sizeof(uint16_t) * k, sizeof(uint16_t));
            tri.index[k] = static_cast<uint16_t>(index);
        }
        pos += rv_mppcmod_triangle_bytes;
    }

    const int checked = rv_mppcmod_int_validate(out, error);
    if (checked != RV_OK) {
        return checked;
    }
    return RV_OK;
}

/// Appends the integer .mppcmod encoding of `model` to `*out`. A model the format cannot
/// hold fails with RV_ERR_INVAL and `error` set, and `*out` is left untouched.
inline int rv_mppcmod_int_write(const rv_mppcmod_int_model &model, std::vector<uint8_t> *out, std::string &error)
{
    const int checked = rv_mppcmod_int_validate(model, error);
    if (checked != RV_OK) {
        return checked;
    }

    out->insert(out->end(), rv_mppcmod_int_magic, rv_mppcmod_int_magic + sizeof(rv_mppcmod_int_magic));
    detail::rv_mppcmod_put_le(out, rv_mppcmod_version, sizeof(uint16_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.parts.size()), sizeof(uint16_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.vertices.size()), sizeof(uint32_t));
    detail::rv_mppcmod_put_le(out, static_cast<uint32_t>(model.triangles.size()), sizeof(uint32_t));
    out->push_back(model.shift);
    out->insert(out->end(), static_cast<size_t>(rv_mppcmod_int_reserved_bytes), uint8_t{ 0 });

    for (const rv_mppcmod_part &part : model.parts) {
        detail::rv_mppcmod_put_le(out, part.first_tri, sizeof(uint32_t));
        detail::rv_mppcmod_put_le(out, part.tri_count, sizeof(uint32_t));
        out->insert(out->end(), part.color.begin(), part.color.end());
        out->push_back(static_cast<uint8_t>(part.texture.size()));
        out->insert(out->end(), part.texture.begin(), part.texture.end());
    }

    for (const rv_mppcmod_int_vertex &vertex : model.vertices) {
        for (int16_t v : vertex.pos) {
            detail::rv_mppcmod_put_le(out, std::bit_cast<uint16_t>(v), sizeof(int16_t));
        }
        for (int16_t v : vertex.uv) {
            detail::rv_mppcmod_put_le(out, std::bit_cast<uint16_t>(v), sizeof(int16_t));
        }
        out->insert(out->end(), vertex.color.begin(), vertex.color.end());
        out->push_back(0); // pad to RV_MPPCMOD_INT_VERT_BYTES
    }

    for (const rv_mppcmod_triangle &tri : model.triangles) {
        for (uint16_t index : tri.index) {
            detail::rv_mppcmod_put_le(out, index, sizeof(uint16_t));
        }
    }
    return RV_OK;
}

} // namespace rv_pdklib
