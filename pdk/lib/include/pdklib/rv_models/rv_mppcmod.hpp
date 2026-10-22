#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdklib
{

// --- the .mppcmod container: shared rules and the float model ---
//
// The integer file layout is in rv_mppcmod_int.hpp; this header holds the common header offsets,
// part and triangle records, name rules and limits that layout builds on, and the float model used
// in memory. Coordinates are the project's (left-handed, Y up, metres): the glTF reader negates z.
// Little-endian.

/// Container version: the PDK version, packed. Readers accept it or an older
/// same-major minor (rv_version_compatible).
inline constexpr uint16_t rv_mppcmod_version = rv_version_packed16;

/// Bits in one byte; the version packs major above it.
inline constexpr int rv_mppcmod_byte_bits = 8;
/// Low byte of a packed version: the minor.
inline constexpr uint32_t rv_mppcmod_minor_mask = 0xFF;

/// Byte offsets inside the common header.
enum rv_mppcmod_offset : int {
    RV_MPPCMOD_OFF_MAGIC = 0,        ///< 4 bytes, "MPMD" or "MPMI"
    RV_MPPCMOD_OFF_VERSION = 4,      ///< uint16, rv_mppcmod_version
    RV_MPPCMOD_OFF_PART_COUNT = 6,   ///< uint16
    RV_MPPCMOD_OFF_VERTEX_COUNT = 8, ///< uint32
    RV_MPPCMOD_OFF_TRI_COUNT = 12    ///< uint32
};

/// The magic of the retired float layout; rv_mppcmod_int_parse refuses it by name.
inline constexpr char rv_mppcmod_magic[4] = { 'M', 'P', 'M', 'D' };

/// File suffix of a baked model on the disc.
inline constexpr const char *rv_mppcmod_extension = ".mppcmod";

/// Offsets inside one part record; the name bytes follow the fixed part.
enum rv_mppcmod_part_offset : int {
    RV_MPPCMOD_PART_OFF_FIRST_TRI = 0, ///< uint32, first triangle
    RV_MPPCMOD_PART_OFF_TRI_COUNT = 4, ///< uint32
    RV_MPPCMOD_PART_OFF_COLOR = 8,     ///< 3 x uint8, RGB
    RV_MPPCMOD_PART_OFF_NAME_LEN = 11, ///< uint8, name bytes, no NUL
    RV_MPPCMOD_PART_FIXED_BYTES = 12   ///< size of everything before the name
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

/// Disc name of a model: the file stem of `source` plus rv_mppcmod_extension, "factory.gltf" -> "factory.mppcmod".
/// Directories follow std::filesystem rules of the platform; a leading dot is part of the name.
inline std::string rv_mppcmod_disc_name(const std::string &source)
{
    return std::filesystem::path(source).stem().string() + rv_mppcmod_extension;
}

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

// The rules parts and triangles share, for the float and the integer model alike: counts fit
// their fields, every triangle index names a vertex, every part's triangles exist, texture names
// fit a u8 length and are plain, and no two parts overlap.
inline int rv_mppcmod_check_records(const std::vector<rv_mppcmod_part> &parts,
    const std::vector<rv_mppcmod_triangle> &triangles,
    size_t vertex_count,
    std::string &error)
{
    if (parts.size() > rv_mppcmod_max_parts) {
        error = "has " + std::to_string(parts.size()) + " parts, more than the 65535 a u16 count holds";
        return RV_ERR_INVAL;
    }
    if (vertex_count > rv_mppcmod_max_vertices) {
        error = "has " + std::to_string(vertex_count) + " vertices, more than the 65535 the format allows";
        return RV_ERR_INVAL;
    }
    if (triangles.size() > std::numeric_limits<uint32_t>::max()) {
        error = "has " + std::to_string(triangles.size()) + " triangles, more than a u32 count holds";
        return RV_ERR_INVAL;
    }
    for (size_t t = 0; t < triangles.size(); ++t) {
        for (uint16_t index : triangles[t].index) {
            if (index >= vertex_count) {
                error = "triangle " + std::to_string(t) + " uses vertex " + std::to_string(index) + ", not one of the " +
                    std::to_string(vertex_count);
                return RV_ERR_INVAL;
            }
        }
    }

    // Parts as [first, end). Sorted by start, a part overlaps an earlier one
    // exactly when it starts before the previous part ends.
    std::vector<std::pair<uint64_t, uint64_t>> spans;
    for (size_t i = 0; i < parts.size(); ++i) {
        const rv_mppcmod_part &part = parts[i];
        const uint64_t end = static_cast<uint64_t>(part.first_tri) + part.tri_count;
        if (end > triangles.size()) {
            error = "part " + std::to_string(i) + " ends at triangle " + std::to_string(end) + ", past the " +
                std::to_string(triangles.size());
            return RV_ERR_INVAL;
        }
        if (part.texture.size() > rv_mppcmod_max_name) {
            error = "part " + std::to_string(i) + " names a texture of " + std::to_string(part.texture.size()) +
                " bytes, more than 255";
            return RV_ERR_INVAL;
        }
        if (!rv_mppcmod_name_is_plain(part.texture)) {
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
} // namespace detail

/// Checks a float model: the shared record rules, then every position and uv is finite.
/// Returns RV_OK, or RV_ERR_INVAL with `error` set to one sentence naming the broken rule.
inline int rv_mppcmod_validate(const rv_mppcmod &model, std::string &error)
{
    const int records = detail::rv_mppcmod_check_records(model.parts, model.triangles, model.vertices.size(), error);
    if (records != RV_OK) {
        return records;
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
    return RV_OK;
}

} // namespace rv_pdklib
