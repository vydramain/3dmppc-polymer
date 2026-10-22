// glTF 2.0 reading, compiled by mppcmodbaker. cgltf is the one place that knows the glTF layout.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "rv_mppcmod_gltf.hpp"

#include "rv_mppcmod_gltf_accessor.hpp"
#include "rv_mppcmod_gltf_image.hpp"
#include "rv_mppcmod_gltf_trs.hpp"

#include "pdklib/rv_models/rv_mppcmod_quant.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_pdklib
{

namespace
{

using detail::entries_within;
using detail::lanes_to_q16;
using detail::q16_matrix;
using detail::RV_MPPCMOD_GLTF_ENTRY_LIMIT;
using detail::RV_MPPCMOD_GLTF_MATRIX_DIM;
using detail::RV_MPPCMOD_GLTF_MATRIX_FLOATS;
using detail::within;
using rv_pdklib::rv_mppcmod_part;
using rv_pdklib::rv_mppcmod_q16_model;
using rv_pdklib::rv_mppcmod_q16_vertex;
using rv_pdklib::rv_mppcmod_triangle;

using gltf_ptr = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

// glTF colours are 0..1; the container stores bytes 0..255.
constexpr float RV_MPPCMOD_GLTF_COLOR_SCALE = 255.0f;
// The colour of a part or vertex that names none: white, so a texture shows as painted.
constexpr std::array<uint8_t, 3> RV_MPPCMOD_GLTF_WHITE = { 255, 255, 255 };
// Components of a position and of a texture coordinate.
constexpr size_t RV_MPPCMOD_GLTF_POS_LANES = 3;
constexpr size_t RV_MPPCMOD_GLTF_UV_LANES = 2;
// Lane of z in a position: glTF is right-handed, the project left-handed, so the reader negates it.
constexpr size_t RV_MPPCMOD_GLTF_DEPTH_LANE = 2;
// Corners of a triangle.
constexpr size_t RV_MPPCMOD_GLTF_CORNERS = 3;
// A closed mesh has about two triangles per vertex, so the tool caps a model at twice the
// vertex limit. This is the tool's own bound, not the format's: a shared index buffer
// repeated by many primitives would otherwise grow the model without limit.
constexpr size_t RV_MPPCMOD_GLTF_MAX_TRIANGLES = 2 * static_cast<size_t>(rv_pdklib::rv_mppcmod_max_vertices);
// Bound in Q.16 on a position: 2^15 m (2^31 in Q.16). An entry below 2^14 times a position below 2^15 m is below
// 2^61, so a sum of three fits int64. The model holds at most 32767 m at shift 0 anyway, so no model that could be
// baked is refused by this bound.
constexpr int64_t RV_MPPCMOD_GLTF_POS_LIMIT = int64_t{ 1 } << 31;
using q16_point = std::array<int64_t, RV_MPPCMOD_GLTF_POS_LANES>;

// What the walk over the scene carries from one primitive to the next.
struct flatten_state {
    rv_mppcmod_q16_model *model = nullptr;
    detail::image_sink sink;
};

uint8_t to_channel(float v)
{
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * RV_MPPCMOD_GLTF_COLOR_SCALE));
}

std::array<uint8_t, 3> to_rgb(const float *c)
{
    return { to_channel(c[0]), to_channel(c[1]), to_channel(c[2]) };
}

std::array<uint8_t, 3> base_color(const cgltf_material *material)
{
    if (material == nullptr || !material->has_pbr_metallic_roughness) {
        return RV_MPPCMOD_GLTF_WHITE;
    }
    return to_rgb(material->pbr_metallic_roughness.base_color_factor);
}

// The accessor of the attribute `type`, set 0; null if the primitive has none.
const cgltf_accessor *find_attribute(const cgltf_primitive &prim, cgltf_attribute_type type)
{
    for (size_t a = 0; a < prim.attributes_count; ++a) {
        if (prim.attributes[a].type == type && prim.attributes[a].index == 0) {
            return prim.attributes[a].data;
        }
    }
    return nullptr;
}

// The identity in Q.16: 1.0 on the diagonal.
q16_matrix identity_q16()
{
    q16_matrix m = {};
    for (size_t d = 0; d < RV_MPPCMOD_GLTF_MATRIX_DIM; ++d) {
        m[(RV_MPPCMOD_GLTF_MATRIX_DIM + 1) * d] = rv_mppcmod_q16_one;
    }
    return m;
}

// out = a * b for column-major matrices, each entry rounded to Q.16 half away from zero.
// A product of two entries is below 2^60, so a sum of four is below 2^62.
void compose(const q16_matrix &a, const q16_matrix &b, q16_matrix &out)
{
    for (size_t col = 0; col < RV_MPPCMOD_GLTF_MATRIX_DIM; ++col) {
        for (size_t row = 0; row < RV_MPPCMOD_GLTF_MATRIX_DIM; ++row) {
            int64_t sum = 0;
            for (size_t k = 0; k < RV_MPPCMOD_GLTF_MATRIX_DIM; ++k) {
                sum += a[RV_MPPCMOD_GLTF_MATRIX_DIM * k + row] * b[RV_MPPCMOD_GLTF_MATRIX_DIM * col + k];
            }
            out[RV_MPPCMOD_GLTF_MATRIX_DIM * col + row] = detail::rv_mppcmod_round_shift(sum, rv_mppcmod_q16_bits);
        }
    }
}

// out = M * (p, 1) in int64, rounded to Q.16. The translation is a Q.16 entry, lifted to Q.32 for the sum.
// RV_ERR_INVAL if a lane reaches the 2^15 m bound.
int transform_point(const q16_matrix &m, const q16_point &p, q16_point &out)
{
    for (size_t row = 0; row < RV_MPPCMOD_GLTF_POS_LANES; ++row) {
        int64_t sum = m[RV_MPPCMOD_GLTF_MATRIX_DIM * RV_MPPCMOD_GLTF_POS_LANES + row] * rv_mppcmod_q16_one;
        for (size_t col = 0; col < RV_MPPCMOD_GLTF_POS_LANES; ++col) {
            sum += m[RV_MPPCMOD_GLTF_MATRIX_DIM * col + row] * p[col];
        }
        out[row] = detail::rv_mppcmod_round_shift(sum, rv_mppcmod_q16_bits);
        if (!within(out[row], RV_MPPCMOD_GLTF_POS_LIMIT)) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// glTF is right-handed (+Z toward the viewer); the project is left-handed (rv_camera.hpp:
// right = cross(up, forward), +z in front). Negating z maps one frame onto the other. The
// camera then sees the model from the side glTF's viewer did, so the windings and the mirror
// test on the glTF world matrix need no change.
void to_project_frame(q16_point &p)
{
    p[RV_MPPCMOD_GLTF_DEPTH_LANE] = -p[RV_MPPCMOD_GLTF_DEPTH_LANE];
}

// True when the 3x3 part of a column-major matrix has a negative determinant. Each of its terms is three
// Q.16 entries, up to 2^90 in Q.48, so int64 cannot hold the sum.
// ponytail: __int128 is the wide type. Clang and gcc provide it on every target built here (Linux, emscripten);
// MSVC has none, and Windows is deferred. Replace it with a 64x64->128 multiply when that target arrives.
bool is_mirrored(const q16_matrix &m)
{
    const auto at = [&m](size_t i) {
        return static_cast<__int128>(m[i]);
    };
    const __int128 det = at(0) * (at(5) * at(10) - at(9) * at(6)) - at(4) * (at(1) * at(10) - at(9) * at(2)) +
        at(8) * (at(1) * at(6) - at(5) * at(2));
    return det < 0;
}

// The local matrix of `node`, each glTF number to Q.16 once. A node with a matrix keeps the file's floats; otherwise
// its TRS is built in int64. A node with no TRS has cgltf's defaults (zero, identity quaternion, unit scale), so the
// build gives the identity.
int local_matrix(const cgltf_node *node, q16_matrix &local)
{
    if (node->has_matrix) {
        return lanes_to_q16(node->matrix, RV_MPPCMOD_GLTF_MATRIX_FLOATS, RV_MPPCMOD_GLTF_ENTRY_LIMIT, local.data());
    }
    return detail::trs_to_q16(node->translation, node->rotation, node->scale, local);
}

// World matrix of `node`: the locals of its ancestors and of itself, composed root to leaf. Each composed matrix is
// checked, so the next product stays inside int64.
int world_matrix(const cgltf_data *data, const cgltf_node *node, q16_matrix &world, std::string &error)
{
    std::vector<const cgltf_node *> chain;
    for (const cgltf_node *n = node; n != nullptr; n = n->parent) {
        chain.push_back(n);
    }
    world = identity_q16();
    for (size_t i = chain.size(); i-- > 0;) {
        const size_t index = static_cast<size_t>(chain[i] - data->nodes);
        q16_matrix local = {};
        if (local_matrix(chain[i], local) != RV_OK) {
            error = "node " + std::to_string(index) + " has a matrix entry at or beyond 2^14 in magnitude, or not finite";
            return RV_ERR_INVAL;
        }
        q16_matrix composed = {};
        compose(world, local, composed);
        if (!entries_within(composed, RV_MPPCMOD_GLTF_ENTRY_LIMIT)) {
            error = "node " + std::to_string(index) + " has a world matrix entry at or beyond 2^14 in magnitude";
            return RV_ERR_INVAL;
        }
        world = composed;
    }
    return RV_OK;
}

// The vertices of one primitive in model space. A missing UV is (0,0), a missing colour white.
int read_vertices(const cgltf_primitive &prim,
    const q16_matrix &world,
    const std::string &where,
    std::vector<rv_mppcmod_q16_vertex> &out,
    std::string &error)
{
    const cgltf_accessor *pos_acc = find_attribute(prim, cgltf_attribute_type_position);
    if (pos_acc == nullptr || pos_acc->type != cgltf_type_vec3) {
        error = where + " has no POSITION of type VEC3";
        return RV_ERR_INVAL;
    }
    const int bound = detail::check_accessor(*pos_acc, where, "POSITION", error);
    if (bound != RV_OK) {
        return bound;
    }
    if (pos_acc->count > rv_pdklib::rv_mppcmod_max_vertices) {
        error = where + " has " + std::to_string(pos_acc->count) + " vertices, more than the 65535 the format allows";
        return RV_ERR_INVAL;
    }
    const size_t count = pos_acc->count;
    std::vector<float> pos;
    int rc = detail::read_attribute(*pos_acc, count, where, "POSITION", pos, error);
    if (rc != RV_OK) {
        return rc;
    }

    const cgltf_accessor *uv_acc = find_attribute(prim, cgltf_attribute_type_texcoord);
    const bool uv_type = uv_acc == nullptr || uv_acc->type == cgltf_type_vec2;
    const cgltf_accessor *color_acc = find_attribute(prim, cgltf_attribute_type_color);
    const bool color_type = color_acc == nullptr || color_acc->type == cgltf_type_vec3 || color_acc->type == cgltf_type_vec4;
    if (!uv_type) {
        error = where + " has a TEXCOORD_0 that is not VEC2";
        return RV_ERR_INVAL;
    }
    if (!color_type) {
        error = where + " has a COLOR_0 that is not VEC3 or VEC4";
        return RV_ERR_INVAL;
    }
    std::vector<float> uv;
    rc = detail::read_optional(uv_acc, count, where, "TEXCOORD_0", uv, error);
    if (rc != RV_OK) {
        return rc;
    }
    std::vector<float> color;
    rc = detail::read_optional(color_acc, count, where, "COLOR_0", color, error);
    if (rc != RV_OK) {
        return rc;
    }
    // Decided once, before the loop: a missing UV stays (0,0), a missing colour stays white.
    const bool has_uv = uv_acc != nullptr;
    const bool has_color = color_acc != nullptr;
    const size_t color_lanes = has_color ? cgltf_num_components(color_acc->type) : 0;
    rv_mppcmod_q16_vertex blank;
    blank.color = RV_MPPCMOD_GLTF_WHITE;
    out.assign(count, blank);
    for (size_t i = 0; i < count; ++i) {
        rv_mppcmod_q16_vertex &vertex = out[i];
        q16_point p = {};
        const float *src = &pos[i * RV_MPPCMOD_GLTF_POS_LANES];
        if (lanes_to_q16(src, RV_MPPCMOD_GLTF_POS_LANES, RV_MPPCMOD_GLTF_POS_LIMIT, p.data()) != RV_OK) {
            error = where + " vertex " + std::to_string(i) + " has a POSITION at or beyond 2^15 m, or not finite";
            return RV_ERR_INVAL;
        }
        if (transform_point(world, p, vertex.pos) != RV_OK) {
            error = where + " vertex " + std::to_string(i) + " has a transformed position at or beyond 2^15 m";
            return RV_ERR_INVAL;
        }
        to_project_frame(vertex.pos);
        if (has_uv) {
            const float *uv_src = &uv[i * RV_MPPCMOD_GLTF_UV_LANES];
            if (lanes_to_q16(uv_src, RV_MPPCMOD_GLTF_UV_LANES, rv_mppcmod_q16_limit, vertex.uv.data()) != RV_OK) {
                error = where + " vertex " + std::to_string(i) +
                    " has a uv component at or beyond 2^40 in magnitude, or not finite";
                return RV_ERR_INVAL;
            }
        }
        if (has_color) {
            vertex.color = to_rgb(&color[i * color_lanes]);
        }
    }
    return RV_OK;
}

// Appends one primitive as one part: its own vertices, its triangles, its colour and texture.
int append_primitive(flatten_state &st,
    const cgltf_primitive &prim,
    const q16_matrix &world,
    const std::string &where,
    std::string &error)
{
    if (prim.type != cgltf_primitive_type_triangles) {
        error = where + " is not a triangle list; only mode triangles is baked";
        return RV_ERR_INVAL;
    }
    if (prim.targets_count > 0) {
        error = where + " has morph targets, which are not baked";
        return RV_ERR_INVAL;
    }
    std::vector<rv_mppcmod_q16_vertex> vertices;
    int rc = read_vertices(prim, world, where, vertices, error);
    if (rc != RV_OK) {
        return rc;
    }
    std::vector<uint32_t> corners;
    rc = detail::read_corners(prim, vertices.size(), where, corners, error);
    if (rc != RV_OK) {
        return rc;
    }
    const size_t total_triangles = st.model->triangles.size() + corners.size() / RV_MPPCMOD_GLTF_CORNERS;
    if (total_triangles > RV_MPPCMOD_GLTF_MAX_TRIANGLES) {
        error = "has " + std::to_string(total_triangles) + " triangles, more than the " +
            std::to_string(RV_MPPCMOD_GLTF_MAX_TRIANGLES) + " this tool bakes";
        return RV_ERR_INVAL;
    }
    const size_t total_vertices = st.model->vertices.size() + vertices.size();
    if (total_vertices > rv_pdklib::rv_mppcmod_max_vertices) {
        error = "has " + std::to_string(total_vertices) + " vertices, more than the 65535 the format allows";
        return RV_ERR_INVAL;
    }
    const size_t total_parts = st.model->parts.size() + 1;
    if (total_parts > rv_pdklib::rv_mppcmod_max_parts) {
        error = "has " + std::to_string(total_parts) + " parts, more than the 65535 a u16 count holds";
        return RV_ERR_INVAL;
    }
    rv_mppcmod_part part;
    rc = detail::bake_texture(st.sink, prim.material, where, part.texture, error);
    if (rc != RV_OK) {
        return rc;
    }
    part.color = base_color(prim.material);
    part.first_tri = static_cast<uint32_t>(st.model->triangles.size());
    part.tri_count = static_cast<uint32_t>(corners.size() / RV_MPPCMOD_GLTF_CORNERS);

    // A mirrored node reverses the winding: glTF front faces are counter-clockwise, and the
    // console culls back faces, so corners 1 and 2 swap to keep the faces the author drew.
    const bool mirrored = is_mirrored(world);
    const size_t first = mirrored ? 2 : 1;
    const size_t second = mirrored ? 1 : 2;

    // Indices are local to the primitive; shift them past the vertices already in the model.
    const size_t base = st.model->vertices.size();
    st.model->vertices.insert(st.model->vertices.end(), vertices.begin(), vertices.end());
    for (size_t c = 0; c < corners.size(); c += RV_MPPCMOD_GLTF_CORNERS) {
        rv_mppcmod_triangle tri;
        tri.index = { static_cast<uint16_t>(base + corners[c]),
            static_cast<uint16_t>(base + corners[c + first]),
            static_cast<uint16_t>(base + corners[c + second]) };
        st.model->triangles.push_back(tri);
    }
    st.model->parts.push_back(part);
    return RV_OK;
}

// The nodes of the scene to bake, in scene order, each once: the default scene, else the first
// scene, else every root node in file order when the file has no scene at all. Depth-first with
// an explicit stack, so a deep chain cannot overflow the call stack. A node comes before its
// children; roots and children are pushed in reverse so they pop in file order.
std::vector<const cgltf_node *> scene_nodes(const cgltf_data *data)
{
    const cgltf_scene *scene = data->scene != nullptr ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    std::vector<const cgltf_node *> stack;
    if (scene != nullptr) {
        for (size_t r = scene->nodes_count; r-- > 0;) {
            stack.push_back(scene->nodes[r]);
        }
    } else {
        for (size_t n = data->nodes_count; n-- > 0;) {
            if (data->nodes[n].parent == nullptr) {
                stack.push_back(&data->nodes[n]);
            }
        }
    }
    std::vector<bool> seen(data->nodes_count, false);
    std::vector<const cgltf_node *> out;
    while (!stack.empty()) {
        const cgltf_node *node = stack.back();
        stack.pop_back();
        const size_t index = static_cast<size_t>(node - data->nodes);
        if (seen[index]) {
            continue;
        }
        seen[index] = true;
        out.push_back(node);
        for (size_t c = node->children_count; c-- > 0;) {
            stack.push_back(node->children[c]);
        }
    }
    return out;
}

// Parses, checks and flattens the scene. Messages name the broken part; the caller adds the path.
int read_gltf(const std::string &path, flatten_state &st, std::string &error)
{
    cgltf_options options = {};
    cgltf_data *raw = nullptr;
    const cgltf_result parsed = cgltf_parse_file(&options, path.c_str(), &raw);
    if (parsed != cgltf_result_success) {
        error = "cannot be read as glTF 2.0 (cgltf result " + std::to_string(parsed) + ")";
        return RV_ERR_INVAL;
    }
    const gltf_ptr data(raw, cgltf_free);
    if (const int rc = detail::check_uris(raw, error); rc != RV_OK) {
        return rc;
    }
    if (cgltf_load_buffers(&options, raw, path.c_str()) != cgltf_result_success) {
        error = "has buffers that cannot be loaded";
        return RV_ERR_INVAL;
    }
    if (cgltf_validate(raw) != cgltf_result_success) {
        error = "is not valid glTF 2.0";
        return RV_ERR_INVAL;
    }

    st.sink.data = raw;
    st.sink.image_seen.assign(raw->images_count, false);
    for (const cgltf_node *node : scene_nodes(raw)) {
        if (node->mesh == nullptr) {
            continue;
        }
        const size_t n = static_cast<size_t>(node - raw->nodes);
        q16_matrix world = {};
        const int world_rc = world_matrix(raw, node, world, error);
        if (world_rc != RV_OK) {
            return world_rc;
        }
        for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
            const std::string where = "node " + std::to_string(n) + ", primitive " + std::to_string(p);
            const int rc = append_primitive(st, node->mesh->primitives[p], world, where, error);
            if (rc != RV_OK) {
                return rc;
            }
        }
    }
    if (st.model->parts.empty()) {
        error = "has no mesh node to bake";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace

int rv_mppcmod_read_gltf(const std::string &path,
    const std::string &stem,
    rv_mppcmod_q16_model &model,
    std::vector<rv_mppcmod_gltf_image> &images,
    std::string &error)
{
    model = rv_mppcmod_q16_model{};
    images.clear();
    flatten_state st;
    st.sink.stem = stem;
    // cgltf's split: the directory runs up to the last '/' or '\'. npos + 1 wraps to 0: no directory.
    st.sink.dir = path.substr(0, path.find_last_of("/\\") + 1);
    st.model = &model;
    st.sink.images = &images;
    const int rc = read_gltf(path, st, error);
    if (rc != RV_OK) {
        error = "'" + path + "' " + error;
        return rc;
    }
    return RV_OK;
}

} // namespace rv_pdklib
