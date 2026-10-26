// glTF accessor reading for rv_mppcmod_gltf.cpp. Compiled by mppcmodbaker and the editor, never by the console. Also
// defines the public rv_mppcmod_gltf_source_vertex that rv_mppcmod_gltf.hpp returns.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod_gltf_trs.hpp"

// Forward declarations keep cgltf.h out of this header: only .cpp files that need its
// definitions include it, so cgltf's types do not leak into every file that includes this.
struct cgltf_accessor;
struct cgltf_primitive;

namespace rv_pdklib
{

// One glTF vertex: pos in metres in the project frame after the node's world matrix; uv as TEXCOORD_0 is stored.
struct rv_mppcmod_gltf_source_vertex {
    std::array<float, 3> pos;
    std::array<float, 2> uv;
};

} // namespace rv_pdklib

namespace rv_pdklib::detail
{

// Each reader refuses what it cannot read with RV_ERR_INVAL and a reason in `error`.
// Sizes are checked against the bytes the file holds before anything is allocated.

// An accessor must name a bufferView; without one there is no data to size or read.
int check_accessor(const cgltf_accessor &acc, const std::string &where, const char *name, std::string &error);

// The floats of an attribute with `count` elements, sparse data included, one row per element.
int read_attribute(const cgltf_accessor &acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error);

// As read_attribute, but a null accessor leaves `out` empty and succeeds.
int read_optional(const cgltf_accessor *acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error);

// The triangle corners of a primitive as indices into its own vertices: the index
// accessor, or 0..vertex_count-1 when the primitive has none.
int read_corners(const cgltf_primitive &prim,
    size_t vertex_count,
    const std::string &where,
    std::vector<uint32_t> &out,
    std::string &error);

// The accessor of the attribute `type` with index 0, or null. `type` is a cgltf_attribute_type;
// int here because this header does not include cgltf.h.
const cgltf_accessor *find_attribute(const cgltf_primitive &prim, int type);

// The vertices of a primitive in accessor order. pos is world * (x, y, z, 1) in double, z negated as
// to_project_frame does, rounded to float once. uv is TEXCOORD_0 as stored, zeros when there is none.
// `world` is column-major, as cgltf gives it.
int read_source_vertices(const cgltf_primitive &prim,
    const std::array<double, RV_MPPCMOD_GLTF_MATRIX_FLOATS> &world,
    const std::string &where,
    std::vector<rv_pdklib::rv_mppcmod_gltf_source_vertex> &out,
    std::string &error);

} // namespace rv_pdklib::detail
