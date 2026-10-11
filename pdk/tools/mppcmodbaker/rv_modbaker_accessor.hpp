// mppcmodbaker: reads glTF accessors into the values the baker needs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations keep cgltf.h out of this header: only .cpp files that need its
// definitions include it, so cgltf's types do not leak into every file that includes this.
struct cgltf_accessor;
struct cgltf_primitive;

// Each reader refuses what it cannot read with RV_ERR_INVAL and a reason in `error`.
// Sizes are checked against the bytes the file holds before anything is allocated.

// An accessor must name a bufferView; without one there is no data to size or read.
int rv_modbaker_check_accessor(const cgltf_accessor &acc, const std::string &where, const char *name, std::string &error);

// The floats of an attribute with `count` elements, sparse data included, one row per element.
int rv_modbaker_read_attribute(const cgltf_accessor &acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error);

// As rv_modbaker_read_attribute, but a null accessor leaves `out` empty and succeeds.
int rv_modbaker_read_optional(const cgltf_accessor *acc,
    size_t count,
    const std::string &where,
    const char *name,
    std::vector<float> &out,
    std::string &error);

// The triangle corners of a primitive as indices into its own vertices: the index
// accessor, or 0..vertex_count-1 when the primitive has none.
int rv_modbaker_read_corners(const cgltf_primitive &prim,
    size_t vertex_count,
    const std::string &where,
    std::vector<uint32_t> &out,
    std::string &error);
