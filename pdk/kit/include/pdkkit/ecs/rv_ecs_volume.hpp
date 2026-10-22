#pragma once

// Point-in-volume test for the scene volumes: the unit cube [-1/2, 1/2]^3 in its own units, placed by the entity's
// world matrix (ecs_component_world). Integer only; the matrix is not inverted.

#include <cstdint>
#include <cstdlib>

#include "pdklib/rv_gte/rv_gte_matrix.hpp"

namespace rv_pdkkit
{

// The cube's half extent 1/2 as a test: |l| <= 1/2 is 2 * |D . c| <= |c|^2, with no division.
inline constexpr std::int64_t ecs_volume_half_inverse = 2;

// True when the Q20.12 point lies in the volume. Each world column c of the matrix is a scaled local axis. The point
// is inside when, for every column, |(p - t) . c| <= |c|^2 / 2: the local coordinate along c is at most 1/2.
// Exact for pairwise perpendicular columns. The 4.12 rounding makes the columns only nearly perpendicular, so a face
// can move by a few Q20.12 units, more when the axis scales differ.
// Not supported: skew (a parent scale that is not uniform under a rotation); the columns are then not perpendicular
// and the test gives the wrong answer. A zero column is a flat volume, so the answer is false.
// Each element of R*S is below 8 in magnitude (rv_gte_trs keeps 4.12 range), so an unrotated volume is under 8 units
// along each axis.
// Bounds: |p - t| below 2^32; each column component is at most 2^15 in magnitude (int16), so |c| is at most sqrt(3)*2^15.
// Products stay in int64: a dot product is below 3*2^47 and 2*|dot| below 2^50.
[[nodiscard]] inline bool ecs_volume_contains(const rv_pdklib::rv_gte_matrix &world, const std::int32_t point[3])
{
    std::int64_t offset[3]{};
    for (int i = 0; i < 3; ++i) {
        offset[i] = static_cast<std::int64_t>(point[i]) - world.t[i];
    }
    for (int k = 0; k < 3; ++k) {
        const std::int64_t column[3] = { world.r[k], world.r[3 + k], world.r[6 + k] };
        const std::int64_t length2 = column[0] * column[0] + column[1] * column[1] + column[2] * column[2];
        if (length2 == 0) {
            return false;
        }
        const std::int64_t dot = offset[0] * column[0] + offset[1] * column[1] + offset[2] * column[2];
        if (ecs_volume_half_inverse * std::llabs(dot) > length2) {
            return false;
        }
    }
    return true;
}

} // namespace rv_pdkkit
