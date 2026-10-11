// glTF accessor reading, compiled by mppcmodbaker. Every size is checked against the bytes the file holds.
#include "rv_mppcmod_gltf_accessor.hpp"

#include "cgltf.h"

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_pdklib::detail
{

namespace
{

// Corners of a triangle.
constexpr size_t RV_MPPCMOD_GLTF_CORNERS = 3;

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

} // namespace rv_pdklib::detail
