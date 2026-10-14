// glTF 2.0 reading, compiled by mppcmodbaker. cgltf is the one place that knows the glTF layout.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "rv_mppcmod_gltf.hpp"

#include "rv_mppcmod_gltf_accessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_pdklib
{

namespace
{

using rv_pdklib::rv_mppcmod;
using rv_pdklib::rv_mppcmod_part;
using rv_pdklib::rv_mppcmod_triangle;
using rv_pdklib::rv_mppcmod_vertex;

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
// glTF matrices are 4x4 and column-major; the last column holds the translation.
constexpr size_t RV_MPPCMOD_GLTF_MATRIX_DIM = 4;
constexpr size_t RV_MPPCMOD_GLTF_MATRIX_FLOATS = RV_MPPCMOD_GLTF_MATRIX_DIM * RV_MPPCMOD_GLTF_MATRIX_DIM;
constexpr std::string_view RV_MPPCMOD_GLTF_MIME_PNG = "image/png";
constexpr std::string_view RV_MPPCMOD_GLTF_MIME_JPEG = "image/jpeg";
constexpr std::string_view RV_MPPCMOD_GLTF_TEXTURE_EXT = ".mppctex";
// A data: URI is embedded in the file, so it is not an external buffer.
constexpr std::string_view RV_MPPCMOD_GLTF_DATA_URI = "data:";
// The eight bytes that open every PNG file.
constexpr std::string_view RV_MPPCMOD_GLTF_PNG_SIGNATURE = "\x89PNG\r\n\x1a\n";
// A PNG file is read whole, so its size is bounded; the limit is far above any console texture.
constexpr std::uintmax_t RV_MPPCMOD_GLTF_MAX_PNG_BYTES = 8 * 1024 * 1024;

// What the walk over the scene carries from one primitive to the next.
struct flatten_state {
    const cgltf_data *data = nullptr;
    std::string stem;
    std::string dir;
    rv_mppcmod *model = nullptr;
    std::vector<rv_mppcmod_gltf_image> *images = nullptr;
    std::vector<bool> image_seen;
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

// The URI as cgltf reads it, with %XX escapes resolved.
std::string decoded_uri(const char *uri)
{
    std::vector<char> buf(uri, uri + std::string_view(uri).size() + 1);
    cgltf_decode_uri(buf.data());
    return buf.data();
}

// data: is embedded. A file must be non-empty, not start with '/' or '\', and have no ':' before its first '/'.
bool is_allowed_uri(const char *uri)
{
    if (uri == nullptr || std::string_view(uri).starts_with(RV_MPPCMOD_GLTF_DATA_URI)) {
        return true;
    }
    const std::string path = decoded_uri(uri);
    if (path.empty() || path[0] == '/' || path[0] == '\\') {
        return false;
    }
    return path.substr(0, path.find('/')).find(':') == std::string::npos;
}

// Every buffer and image URI must pass is_allowed_uri; the error names the first that does not.
int check_uris(const cgltf_data *data, std::string &error)
{
    for (size_t b = 0; b < data->buffers_count; ++b) {
        if (!is_allowed_uri(data->buffers[b].uri)) {
            error = "buffer " + std::to_string(b) + " has a URI that is neither a relative file path nor data:";
            return RV_ERR_INVAL;
        }
    }
    for (size_t i = 0; i < data->images_count; ++i) {
        if (!is_allowed_uri(data->images[i].uri)) {
            error = "image " + std::to_string(i) + " has a URI that is neither a relative file path nor data:";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// out = M * (p, 1) for the column-major matrix M.
void transform_point(const float *m, const float *p, float *out)
{
    for (size_t row = 0; row < RV_MPPCMOD_GLTF_POS_LANES; ++row) {
        float sum = m[RV_MPPCMOD_GLTF_MATRIX_DIM * RV_MPPCMOD_GLTF_POS_LANES + row];
        for (size_t col = 0; col < RV_MPPCMOD_GLTF_POS_LANES; ++col) {
            sum += m[RV_MPPCMOD_GLTF_MATRIX_DIM * col + row] * p[col];
        }
        out[row] = sum;
    }
}

// glTF is right-handed (+Z toward the viewer); the project is left-handed (rv_camera.hpp:
// right = cross(up, forward), +z in front). Negating z maps one frame onto the other. The
// camera then sees the model from the side glTF's viewer did, so the windings and the mirror
// test on the glTF world matrix need no change.
void to_project_frame(float *p)
{
    p[RV_MPPCMOD_GLTF_DEPTH_LANE] = -p[RV_MPPCMOD_GLTF_DEPTH_LANE];
}

// Determinant of the 3x3 part of a column-major matrix; negative when the matrix mirrors.
float determinant3(const float *m)
{
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// The vertices of one primitive in model space. A missing UV is (0,0), a missing colour white.
int read_vertices(const cgltf_primitive &prim,
    const float *world,
    const std::string &where,
    std::vector<rv_mppcmod_vertex> &out,
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
    rv_mppcmod_vertex blank;
    blank.color = RV_MPPCMOD_GLTF_WHITE;
    out.assign(count, blank);
    for (size_t i = 0; i < count; ++i) {
        rv_mppcmod_vertex &vertex = out[i];
        transform_point(world, &pos[i * RV_MPPCMOD_GLTF_POS_LANES], vertex.pos.data());
        to_project_frame(vertex.pos.data());
        if (has_uv) {
            vertex.uv = { uv[i * RV_MPPCMOD_GLTF_UV_LANES], uv[i * RV_MPPCMOD_GLTF_UV_LANES + 1] };
        }
        if (has_color) {
            vertex.color = to_rgb(&color[i * color_lanes]);
        }
    }
    return RV_OK;
}

// The PNG bytes of an image: its bufferView (a .glb embeds it), else the regular file its URI names.
int image_bytes(const flatten_state &st,
    const cgltf_image &image,
    const std::string &where,
    std::vector<uint8_t> &out,
    std::string &error)
{
    const cgltf_buffer_view *view = image.buffer_view;
    const auto *src = view != nullptr ? static_cast<const uint8_t *>(cgltf_buffer_view_data(view)) : nullptr;
    if (src != nullptr) {
        out.assign(src, src + view->size);
        return RV_OK;
    }
    if (view != nullptr) {
        error = where + " has no data";
        return RV_ERR_INVAL;
    }
    if (image.uri == nullptr || std::string_view(image.uri).starts_with(RV_MPPCMOD_GLTF_DATA_URI)) {
        error = where + " has no bufferView or file; the baker reads images from one (a .glb embeds them)";
        return RV_ERR_INVAL;
    }
    const std::filesystem::path file = std::filesystem::path(st.dir) / decoded_uri(image.uri);
    // Only a regular file of bounded size is read: a FIFO, a device or a directory would block or never end.
    std::error_code ec;
    const bool regular = std::filesystem::is_regular_file(file, ec);
    const std::uintmax_t size = regular ? std::filesystem::file_size(file, ec) : 0;
    if (!regular || ec || size > RV_MPPCMOD_GLTF_MAX_PNG_BYTES) {
        error = where + " file '" + image.uri + "' is missing, not a regular file, or over the size limit";
        return RV_ERR_INVAL;
    }
    std::ifstream in(file, std::ios::binary);
    out.resize(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size))) {
        error = where + " file '" + image.uri + "' cannot be read";
        return RV_ERR_INVAL;
    }
    if (!std::string_view(reinterpret_cast<const char *>(out.data()), out.size()).starts_with(RV_MPPCMOD_GLTF_PNG_SIGNATURE)) {
        error = where + " file '" + image.uri + "' is not PNG";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Names the texture of `material` and keeps its PNG once. Leaves `texture` empty when
// the material has none.
int bake_texture(flatten_state &st,
    const cgltf_material *material,
    const std::string &where,
    std::string &texture,
    std::string &error)
{
    texture.clear();
    if (material == nullptr || !material->has_pbr_metallic_roughness) {
        return RV_OK;
    }
    const cgltf_texture_view &view = material->pbr_metallic_roughness.base_color_texture;
    if (view.texture == nullptr) {
        return RV_OK;
    }
    if (view.texcoord != 0) {
        error = where + " uses TEXCOORD_" + std::to_string(view.texcoord) + " for its texture; only TEXCOORD_0 is baked";
        return RV_ERR_INVAL;
    }
    const cgltf_image *image = view.texture->image;
    if (image == nullptr) {
        error = "has a texture with no image";
        return RV_ERR_INVAL;
    }
    const size_t index = static_cast<size_t>(image - st.data->images);
    const std::string image_where = "image " + std::to_string(index);
    const std::string_view mime = image->mime_type != nullptr ? std::string_view(image->mime_type) : std::string_view();
    // A file may omit its MIME type; its signature is checked when it is read.
    const bool mime_ok = mime == RV_MPPCMOD_GLTF_MIME_PNG || (image->buffer_view == nullptr && mime.empty());
    if (!mime_ok) {
        const std::string kind = mime == RV_MPPCMOD_GLTF_MIME_JPEG ? "JPEG" : mime.empty() ? "no MIME type" : std::string(mime);
        error = image_where + " is " + kind + ", not PNG; export the model with PNG textures";
        return RV_ERR_INVAL;
    }

    const std::string name = st.stem + "-" + std::to_string(index);
    if (!st.image_seen[index]) {
        rv_mppcmod_gltf_image out;
        out.name = name;
        const int rc = image_bytes(st, *image, image_where, out.png, error);
        if (rc != RV_OK) {
            return rc;
        }
        st.images->push_back(std::move(out));
        st.image_seen[index] = true;
    }
    texture = name + std::string(RV_MPPCMOD_GLTF_TEXTURE_EXT);
    return RV_OK;
}

// Appends one primitive as one part: its own vertices, its triangles, its colour and texture.
int append_primitive(flatten_state &st,
    const cgltf_primitive &prim,
    const float *world,
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
    std::vector<rv_mppcmod_vertex> vertices;
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
    rc = bake_texture(st, prim.material, where, part.texture, error);
    if (rc != RV_OK) {
        return rc;
    }
    part.color = base_color(prim.material);
    part.first_tri = static_cast<uint32_t>(st.model->triangles.size());
    part.tri_count = static_cast<uint32_t>(corners.size() / RV_MPPCMOD_GLTF_CORNERS);

    // A mirrored node reverses the winding: glTF front faces are counter-clockwise, and the
    // console culls back faces, so corners 1 and 2 swap to keep the faces the author drew.
    const bool mirrored = determinant3(world) < 0.0f;
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
    if (const int rc = check_uris(raw, error); rc != RV_OK) {
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

    st.data = raw;
    st.image_seen.assign(raw->images_count, false);
    for (const cgltf_node *node : scene_nodes(raw)) {
        if (node->mesh == nullptr) {
            continue;
        }
        const size_t n = static_cast<size_t>(node - raw->nodes);
        float world[RV_MPPCMOD_GLTF_MATRIX_FLOATS];
        cgltf_node_transform_world(node, world);
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
    rv_mppcmod &model,
    std::vector<rv_mppcmod_gltf_image> &images,
    std::string &error)
{
    model = rv_mppcmod{};
    images.clear();
    flatten_state st;
    st.stem = stem;
    // cgltf's split: the directory runs up to the last '/' or '\'. npos + 1 wraps to 0: no directory.
    st.dir = path.substr(0, path.find_last_of("/\\") + 1);
    st.model = &model;
    st.images = &images;
    const int rc = read_gltf(path, st, error);
    if (rc != RV_OK) {
        error = "'" + path + "' " + error;
        return rc;
    }
    return RV_OK;
}

} // namespace rv_pdklib
