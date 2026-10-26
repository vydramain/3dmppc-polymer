// glTF accessor reading, compiled by mppcmodbaker and the editor, never by the console. Every size is checked against the bytes
// the file holds.
#include "rv_mppcmod_gltf_accessor.hpp"

#include "cgltf.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"

namespace rv_pdklib::detail
{

namespace
{

// Corners of a triangle.
constexpr size_t RV_MPPCMOD_GLTF_CORNERS = 3;

// Lanes of a source position and of a uv, and the depth lane that to_project_frame negates.
constexpr size_t RV_MPPCMOD_GLTF_POS_LANES = 3;
constexpr size_t RV_MPPCMOD_GLTF_UV_LANES = 2;
constexpr size_t RV_MPPCMOD_GLTF_DEPTH_LANE = 2;
// The w of a point: 1, so the translation column adds to each row.
constexpr double RV_MPPCMOD_GLTF_POINT_W = 1.0;

// Corners must form whole triangles and name vertices of their own primitive.
int check_corners(const std::vector<uint32_t> &corners, size_t vertex_count, const std::string &where, std::string &error)
{
    if (corners.size() % RV_MPPCMOD_GLTF_CORNERS != 0) {
        error = where + " has " + std::to_string(corners.size()) + " indices, not a multiple of 3";
        return RV_ERR_INVAL;
    }
    for (uint32_t index : corners) {
        if (index >= vertex_count) {
            error = where + " uses vertex " + std::to_string(index) + ", past its " + std::to_string(vertex_count);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Floats of `acc`, sparse data included, one row per element. Refuses an accessor
// whose element count is not `count`, and one cgltf cannot read.
int unpack(const cgltf_accessor &acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error)
{
    if (acc.count != count) {
        error = where + " " + name + " has " + std::to_string(acc.count) + " elements, not " + std::to_string(count);
        return RV_ERR_INVAL;
    }
    out.assign(count * cgltf_num_components(acc.type), 0.0f);
    const size_t written = cgltf_accessor_unpack_floats(&acc, out.data(), out.size());
    if (written != out.size()) {
        error = where + " " + name + " cannot be read";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The index accessor as corners. Its bytes bound its size: each index takes at least one byte.
int read_indices(const cgltf_accessor &acc,
    size_t vertex_count,
    const std::string &where,
    std::vector<uint32_t> &out,
    std::string &error)
{
    const int rc = check_accessor(acc, where, "indices", error);
    if (rc != RV_OK) {
        return rc;
    }
    if (acc.type != cgltf_type_scalar) {
        error = where + " has indices that are not SCALAR";
        return RV_ERR_INVAL;
    }
    if (acc.count > acc.buffer_view->size) {
        error = where + " has " + std::to_string(acc.count) + " indices, more than its bufferView holds";
        return RV_ERR_INVAL;
    }
    out.assign(acc.count, 0);
    const size_t written = cgltf_accessor_unpack_indices(&acc, out.data(), sizeof(uint32_t), out.size());
    if (written != out.size()) {
        error = where + " indices cannot be read";
        return RV_ERR_INVAL;
    }
    return check_corners(out, vertex_count, where, error);
}

// world * (x, y, z, 1) in double, depth negated, then each lane rounded to float once.
// Fails when a lane is not finite or leaves float range: the cast past it is undefined.
int transform_position(const std::array<double, RV_MPPCMOD_GLTF_MATRIX_FLOATS> &world,
    const float *src,
    std::array<float, RV_MPPCMOD_GLTF_POS_LANES> &out)
{
    const std::array<double, RV_MPPCMOD_GLTF_MATRIX_DIM> point = { src[0], src[1], src[2], RV_MPPCMOD_GLTF_POINT_W };
    std::array<double, RV_MPPCMOD_GLTF_POS_LANES> p = {};
    for (size_t row = 0; row < RV_MPPCMOD_GLTF_POS_LANES; ++row) {
        for (size_t col = 0; col < RV_MPPCMOD_GLTF_MATRIX_DIM; ++col) {
            p[row] += world[col * RV_MPPCMOD_GLTF_MATRIX_DIM + row] * point[col];
        }
    }
    p[RV_MPPCMOD_GLTF_DEPTH_LANE] = -p[RV_MPPCMOD_GLTF_DEPTH_LANE];
    for (size_t lane = 0; lane < RV_MPPCMOD_GLTF_POS_LANES; ++lane) {
        if (!std::isfinite(p[lane]) || std::fabs(p[lane]) > static_cast<double>(std::numeric_limits<float>::max())) {
            return RV_ERR_INVAL;
        }
        out[lane] = static_cast<float>(p[lane]);
    }
    return RV_OK;
}

} // namespace

int check_accessor(const cgltf_accessor &acc, const std::string &where, const char *name, std::string &error)
{
    if (acc.buffer_view == nullptr) {
        error = where + " " + name + " has no data";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int read_attribute(const cgltf_accessor &acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error)
{
    const int rc = check_accessor(acc, where, name, error);
    if (rc != RV_OK) {
        return rc;
    }
    return unpack(acc, count, where, name, out, error);
}

int read_optional(const cgltf_accessor *acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error)
{
    if (acc == nullptr) {
        return RV_OK;
    }
    return read_attribute(*acc, count, where, name, out, error);
}

int read_corners(const cgltf_primitive &prim,
    size_t vertex_count,
    const std::string &where,
    std::vector<uint32_t> &out,
    std::string &error)
{
    if (prim.indices != nullptr) {
        return read_indices(*prim.indices, vertex_count, where, out, error);
    }
    out.resize(vertex_count);
    std::iota(out.begin(), out.end(), uint32_t{ 0 });
    return check_corners(out, vertex_count, where, error);
}

const cgltf_accessor *find_attribute(const cgltf_primitive &prim, int type)
{
    for (size_t a = 0; a < prim.attributes_count; ++a) {
        if (prim.attributes[a].type == type && prim.attributes[a].index == 0) {
            return prim.attributes[a].data;
        }
    }
    return nullptr;
}

int read_source_vertices(const cgltf_primitive &prim,
    const std::array<double, RV_MPPCMOD_GLTF_MATRIX_FLOATS> &world,
    const std::string &where,
    std::vector<rv_pdklib::rv_mppcmod_gltf_source_vertex> &out,
    std::string &error)
{
    const cgltf_accessor *pos_acc = find_attribute(prim, cgltf_attribute_type_position);
    if (pos_acc == nullptr || pos_acc->type != cgltf_type_vec3) {
        error = where + " has no POSITION of type VEC3";
        return RV_ERR_INVAL;
    }
    const cgltf_accessor *uv_acc = find_attribute(prim, cgltf_attribute_type_texcoord);
    if (uv_acc != nullptr && uv_acc->type != cgltf_type_vec2) {
        error = where + " has a TEXCOORD_0 that is not VEC2";
        return RV_ERR_INVAL;
    }
    const size_t count = pos_acc->count;
    if (count > rv_pdklib::rv_mppcmod_max_vertices) {
        error = where + " has " + std::to_string(count) + " vertices, more than the 65535 the format allows";
        return RV_ERR_INVAL;
    }
    std::vector<float> pos;
    int rc = read_attribute(*pos_acc, count, where, "POSITION", pos, error);
    if (rc != RV_OK) {
        return rc;
    }
    std::vector<float> uv;
    rc = read_optional(uv_acc, count, where, "TEXCOORD_0", uv, error);
    if (rc != RV_OK) {
        return rc;
    }
    // Decided once, before the loop: a missing TEXCOORD_0 leaves every uv at (0, 0).
    const bool has_uv = uv_acc != nullptr;
    out.assign(count, rv_pdklib::rv_mppcmod_gltf_source_vertex{});
    for (size_t i = 0; i < count; ++i) {
        rv_pdklib::rv_mppcmod_gltf_source_vertex &vertex = out[i];
        if (transform_position(world, &pos[i * RV_MPPCMOD_GLTF_POS_LANES], vertex.pos) != RV_OK) {
            error =
                where + " vertex " + std::to_string(i) + " has a transformed position that is not finite or leaves float range";
            return RV_ERR_INVAL;
        }
        if (has_uv) {
            for (size_t lane = 0; lane < RV_MPPCMOD_GLTF_UV_LANES; ++lane) {
                vertex.uv[lane] = uv[i * RV_MPPCMOD_GLTF_UV_LANES + lane];
            }
        }
    }
    return RV_OK;
}

} // namespace rv_pdklib::detail
