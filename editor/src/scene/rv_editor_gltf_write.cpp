// Writes selected triangles of a model as glTF 2.0 Separate. The reader rv_mppcmod_read_gltf is the inverse.

#include "scene/rv_editor_gltf_write.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

using rv_pdklib::rv_mppcmod;
using rv_pdklib::rv_mppcmod_part;
using rv_pdklib::rv_mppcmod_vertex;

// glTF enums from the 2.0 specification.
constexpr int RV_EDITOR_GLTF_FLOAT = 5126;                 // accessor componentType FLOAT
constexpr int RV_EDITOR_GLTF_USHORT = 5123;                // accessor componentType UNSIGNED_SHORT
constexpr int RV_EDITOR_GLTF_ARRAY_BUFFER = 34962;         // bufferView target of vertex attributes
constexpr int RV_EDITOR_GLTF_ELEMENT_ARRAY_BUFFER = 34963; // bufferView target of indices
constexpr int RV_EDITOR_GLTF_TRIANGLES = 4;                // primitive mode: a triangle list

// Lanes per attribute, and indices per triangle.
constexpr size_t RV_EDITOR_GLTF_POS_LANES = 3;
constexpr size_t RV_EDITOR_GLTF_UV_LANES = 2;
constexpr size_t RV_EDITOR_GLTF_COLOR_LANES = 3;
constexpr size_t RV_EDITOR_GLTF_CORNERS = 3;
// Lane of z in a position. glTF is right-handed, the project left-handed: z is negated, and the reader negates it back.
constexpr size_t RV_EDITOR_GLTF_DEPTH_LANE = 2;
// Colour bytes 0..255 are written as glTF's 0..1; the reader multiplies back by 255.
constexpr float RV_EDITOR_GLTF_COLOR_SCALE = 255.0f;
// Opaque alpha in baseColorFactor.
constexpr float RV_EDITOR_GLTF_ALPHA = 1.0f;

// bufferView and accessor numbers in the JSON. The index accessor of run k is FIRST_INDEX_ACCESSOR + k.
constexpr size_t RV_EDITOR_GLTF_POS_VIEW = 0;
constexpr size_t RV_EDITOR_GLTF_UV_VIEW = 1;
constexpr size_t RV_EDITOR_GLTF_COLOR_VIEW = 2;
constexpr size_t RV_EDITOR_GLTF_INDEX_VIEW = 3;
constexpr size_t RV_EDITOR_GLTF_POS_ACCESSOR = 0;
constexpr size_t RV_EDITOR_GLTF_UV_ACCESSOR = 1;
constexpr size_t RV_EDITOR_GLTF_COLOR_ACCESSOR = 2;
constexpr size_t RV_EDITOR_GLTF_FIRST_INDEX_ACCESSOR = 3;

// A vertex index that no output vertex has yet.
constexpr size_t RV_EDITOR_GLTF_UNMAPPED = std::numeric_limits<size_t>::max();
constexpr std::string_view RV_EDITOR_GLTF_EXT = ".gltf";
constexpr std::string_view RV_EDITOR_GLTF_BIN_EXT = ".bin";
// The glTF asset version this writer emits.
constexpr std::string_view RV_EDITOR_GLTF_VERSION = "2.0";

// The selected triangles of one part, in model order.
struct run {
    std::array<uint8_t, 3> color = {};
    std::string texture;
    std::vector<uint32_t> tris;
    size_t first_index = 0; // into gltf_mesh::indices
};

// One vertex list and one index list shared by all runs; positions are in glTF's frame.
struct gltf_mesh {
    std::vector<run> runs;
    std::vector<rv_mppcmod_vertex> verts;
    std::vector<uint16_t> indices;
    std::array<float, RV_EDITOR_GLTF_POS_LANES> pos_min = {};
    std::array<float, RV_EDITOR_GLTF_POS_LANES> pos_max = {};
};

// Byte offsets of the attribute blocks in the .bin, as build_bin writes them.
struct layout {
    size_t uv_at = 0;
    size_t color_at = 0;
    size_t index_at = 0;
    size_t index_bytes = 0;
};

// The JSON elements of the file, each joined by commas.
struct json_lists {
    std::string materials;
    std::string primitives;
    std::string images;
    std::string textures;
    std::string views;
    std::string accessors;
};

// Appends a comma-separated JSON element.
void append_item(std::string &list, const std::string &item)
{
    if (!list.empty()) {
        list += ',';
    }
    list += item;
}

// A JSON string literal; only quote, backslash and control bytes need an escape.
std::string json_string(std::string_view s)
{
    std::string out = "\"";
    for (const char c : s) {
        const auto uc = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (uc < rv_pdklib::rv_mppcmod_first_printable) {
            out += std::format("\\u{:04x}", uc);
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// A URI the reader takes as written: relative, no escape ('%'), no scheme (':'), not absolute.
bool plain_uri(std::string_view uri)
{
    return !uri.empty() && uri.find_first_of("%:") == std::string_view::npos && uri.front() != '/' && uri.front() != '\\';
}

// Both targets must be new: the pair of renames is not atomic, so an old pair could be left half replaced.
int require_new(const std::filesystem::path &gltf, const std::filesystem::path &bin, std::string &error)
{
    for (const std::filesystem::path *target : { &gltf, &bin }) {
        std::error_code ec;
        const bool present = std::filesystem::exists(*target, ec);
        if (present || ec) {
            error = target->string() + " already exists or cannot be checked; the write needs new files";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Picks the selected triangles of each part. A selected triangle no part holds is refused:
// the file could not give it a colour or a texture.
int pick_runs(const rv_mppcmod &model, const std::vector<uint32_t> &tris, std::vector<run> &runs, std::string &error)
{
    std::vector<bool> chosen(model.triangles.size(), false);
    for (const uint32_t t : tris) {
        if (t >= model.triangles.size()) {
            error =
                "triangle " + std::to_string(t) + " is out of range: the model has " + std::to_string(model.triangles.size());
            return RV_ERR_INVAL;
        }
        chosen[t] = true;
    }
    const size_t wanted = static_cast<size_t>(std::count(chosen.begin(), chosen.end(), true));
    size_t kept = 0;
    for (const rv_mppcmod_part &part : model.parts) {
        run r;
        r.color = part.color;
        r.texture = part.texture;
        const size_t end = static_cast<size_t>(part.first_tri) + part.tri_count;
        for (size_t t = part.first_tri; t < end; ++t) {
            if (chosen[t]) {
                r.tris.push_back(static_cast<uint32_t>(t));
            }
        }
        kept += r.tris.size();
        if (!r.tris.empty()) {
            runs.push_back(std::move(r));
        }
    }
    if (kept != wanted) {
        error = std::to_string(wanted - kept) + " selected triangles belong to no part";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Copies the used vertices in first-use order, negating z, and builds the index list. pick_runs
// leaves at least one run, and every run has a triangle, so mesh.verts is never empty below.
void build_mesh(const rv_mppcmod &model, gltf_mesh &mesh)
{
    std::vector<size_t> remap(model.vertices.size(), RV_EDITOR_GLTF_UNMAPPED);
    for (run &r : mesh.runs) {
        r.first_index = mesh.indices.size();
        for (const uint32_t t : r.tris) {
            for (const uint16_t v : model.triangles[t].index) {
                if (remap[v] == RV_EDITOR_GLTF_UNMAPPED) {
                    remap[v] = mesh.verts.size();
                    mesh.verts.push_back(model.vertices[v]);
                    rv_mppcmod_vertex &copy = mesh.verts.back();
                    copy.pos[RV_EDITOR_GLTF_DEPTH_LANE] = -copy.pos[RV_EDITOR_GLTF_DEPTH_LANE];
                }
                mesh.indices.push_back(static_cast<uint16_t>(remap[v]));
            }
        }
    }
    mesh.pos_min = mesh.verts[0].pos;
    mesh.pos_max = mesh.verts[0].pos;
    for (const rv_mppcmod_vertex &vertex : mesh.verts) {
        for (size_t k = 0; k < RV_EDITOR_GLTF_POS_LANES; ++k) {
            mesh.pos_min[k] = std::min(mesh.pos_min[k], vertex.pos[k]);
            mesh.pos_max[k] = std::max(mesh.pos_max[k], vertex.pos[k]);
        }
    }
}

// The .bin: positions, uvs and colours per vertex, then the indices. Little-endian, in the order
// make_layout names: positions at 0, uvs, colours, indices.
void build_bin(const gltf_mesh &mesh, std::vector<uint8_t> &bin)
{
    const auto put_float = [&bin](float f) {
        rv_pdklib::detail::rv_mppcmod_put_le(&bin, std::bit_cast<uint32_t>(f), sizeof(float));
    };
    for (const rv_mppcmod_vertex &v : mesh.verts) {
        for (const float f : v.pos) {
            put_float(f);
        }
    }
    for (const rv_mppcmod_vertex &v : mesh.verts) {
        for (const float f : v.uv) {
            put_float(f);
        }
    }
    for (const rv_mppcmod_vertex &v : mesh.verts) {
        for (const uint8_t c : v.color) {
            put_float(static_cast<float>(c) / RV_EDITOR_GLTF_COLOR_SCALE);
        }
    }
    for (const uint16_t index : mesh.indices) {
        rv_pdklib::detail::rv_mppcmod_put_le(&bin, index, sizeof(uint16_t));
    }
}

layout make_layout(const gltf_mesh &mesh)
{
    const size_t n = mesh.verts.size();
    layout lay;
    lay.uv_at = n * RV_EDITOR_GLTF_POS_LANES * sizeof(float);
    lay.color_at = lay.uv_at + n * RV_EDITOR_GLTF_UV_LANES * sizeof(float);
    lay.index_at = lay.color_at + n * RV_EDITOR_GLTF_COLOR_LANES * sizeof(float);
    lay.index_bytes = mesh.indices.size() * sizeof(uint16_t);
    return lay;
}

std::string view_json(size_t offset, size_t bytes, int target)
{
    return std::format("{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{},\"target\":{}}}", offset, bytes, target);
}

// One bufferView per block of the .bin, in the order make_layout gives.
void build_views(const layout &lay, std::string &views)
{
    append_item(views, view_json(0, lay.uv_at, RV_EDITOR_GLTF_ARRAY_BUFFER));
    append_item(views, view_json(lay.uv_at, lay.color_at - lay.uv_at, RV_EDITOR_GLTF_ARRAY_BUFFER));
    append_item(views, view_json(lay.color_at, lay.index_at - lay.color_at, RV_EDITOR_GLTF_ARRAY_BUFFER));
    append_item(views, view_json(lay.index_at, lay.index_bytes, RV_EDITOR_GLTF_ELEMENT_ARRAY_BUFFER));
}

// The attribute accessors (min and max on POSITION, which glTF requires), then each run's indices.
void build_accessors(const gltf_mesh &mesh, std::string &accessors)
{
    const size_t n = mesh.verts.size();
    const auto bounds = [](const std::array<float, RV_EDITOR_GLTF_POS_LANES> &a) {
        return std::format("[{},{},{}]", a[0], a[1], a[2]);
    };
    append_item(accessors,
        std::format("{{\"bufferView\":{},\"componentType\":{},\"count\":{},\"type\":\"VEC3\",\"min\":{},\"max\":{}}}",
            RV_EDITOR_GLTF_POS_VIEW,
            RV_EDITOR_GLTF_FLOAT,
            n,
            bounds(mesh.pos_min),
            bounds(mesh.pos_max)));
    append_item(accessors,
        std::format("{{\"bufferView\":{},\"componentType\":{},\"count\":{},\"type\":\"VEC2\"}}",
            RV_EDITOR_GLTF_UV_VIEW,
            RV_EDITOR_GLTF_FLOAT,
            n));
    append_item(accessors,
        std::format("{{\"bufferView\":{},\"componentType\":{},\"count\":{},\"type\":\"VEC3\"}}",
            RV_EDITOR_GLTF_COLOR_VIEW,
            RV_EDITOR_GLTF_FLOAT,
            n));
    for (const run &r : mesh.runs) {
        append_item(accessors,
            std::format("{{\"bufferView\":{},\"byteOffset\":{},\"componentType\":{},\"count\":{},\"type\":\"SCALAR\"}}",
                RV_EDITOR_GLTF_INDEX_VIEW,
                r.first_index * sizeof(uint16_t),
                RV_EDITOR_GLTF_USHORT,
                r.tris.size() * RV_EDITOR_GLTF_CORNERS));
    }
}

// One primitive and one material per run; a textured run adds an image and a texture per texture name.
int build_materials(const gltf_mesh &mesh, const std::map<std::string, std::string> &uris, json_lists &out, std::string &error)
{
    std::map<std::string, size_t> image_of; // texture name -> image index
    for (size_t k = 0; k < mesh.runs.size(); ++k) {
        const run &r = mesh.runs[k];
        std::string texture_ref;
        if (!r.texture.empty()) {
            const auto uri = uris.find(r.texture);
            if (uri == uris.end()) {
                error = "texture '" + r.texture + "' has no URI";
                return RV_ERR_INVAL;
            }
            if (!plain_uri(uri->second)) {
                error = "texture '" + r.texture + "' has URI '" + uri->second +
                    "', which is not a plain relative path (no '%', ':' or leading '/')";
                return RV_ERR_INVAL;
            }
            const auto [slot, fresh] = image_of.try_emplace(r.texture, image_of.size());
            if (fresh) {
                append_item(out.images, std::format("{{\"uri\":{}}}", json_string(uri->second)));
                append_item(out.textures, std::format("{{\"source\":{}}}", slot->second));
            }
            texture_ref = std::format(",\"baseColorTexture\":{{\"index\":{}}}", slot->second);
        }
        const std::array<float, 4> factor = { static_cast<float>(r.color[0]) / RV_EDITOR_GLTF_COLOR_SCALE,
            static_cast<float>(r.color[1]) / RV_EDITOR_GLTF_COLOR_SCALE,
            static_cast<float>(r.color[2]) / RV_EDITOR_GLTF_COLOR_SCALE,
            RV_EDITOR_GLTF_ALPHA };
        append_item(out.materials,
            std::format("{{\"pbrMetallicRoughness\":{{\"baseColorFactor\":[{},{},{},{}]{}}}}}",
                factor[0],
                factor[1],
                factor[2],
                factor[3],
                texture_ref));
        append_item(out.primitives,
            std::format("{{\"attributes\":{{\"POSITION\":{},\"TEXCOORD_0\":{},\"COLOR_0\":{}}},\"indices\":{},"
                        "\"material\":{},\"mode\":{}}}",
                RV_EDITOR_GLTF_POS_ACCESSOR,
                RV_EDITOR_GLTF_UV_ACCESSOR,
                RV_EDITOR_GLTF_COLOR_ACCESSOR,
                RV_EDITOR_GLTF_FIRST_INDEX_ACCESSOR + k,
                k,
                RV_EDITOR_GLTF_TRIANGLES));
    }
    return RV_OK;
}

// The JSON text of the file. Empty lists are left out: glTF arrays may not be empty.
int build_json(const gltf_mesh &mesh,
    const std::map<std::string, std::string> &uris,
    const std::string &bin_name,
    std::string &json,
    std::string &error)
{
    json_lists lists;
    const int built = build_materials(mesh, uris, lists, error);
    if (built != RV_OK) {
        return built;
    }
    const layout lay = make_layout(mesh);
    build_views(lay, lists.views);
    build_accessors(mesh, lists.accessors);
    const std::string textured =
        lists.images.empty() ? std::string() : std::format(",\"textures\":[{}],\"images\":[{}]", lists.textures, lists.images);
    json = std::format("{{\"asset\":{{\"version\":{}}},\"scene\":0,\"scenes\":[{{\"nodes\":[0]}}],\"nodes\":[{{\"mesh\":0}}],"
                       "\"meshes\":[{{\"primitives\":[{}]}}],\"materials\":[{}]{},\"accessors\":[{}],\"bufferViews\":[{}],"
                       "\"buffers\":[{{\"uri\":{},\"byteLength\":{}}}]}}",
        json_string(RV_EDITOR_GLTF_VERSION),
        lists.primitives,
        lists.materials,
        textured,
        lists.accessors,
        lists.views,
        json_string(bin_name),
        lay.index_at + lay.index_bytes);
    return RV_OK;
}

} // namespace

int rv_editor_gltf_write(const std::filesystem::path &gltf,
    const rv_mppcmod &model,
    const std::vector<uint32_t> &tris,
    const std::map<std::string, std::string> &texture_uri,
    std::string &error)
{
    if (gltf.extension().string() != RV_EDITOR_GLTF_EXT) {
        error = gltf.string() + " is not a .gltf file";
        return RV_ERR_INVAL;
    }
    if (tris.empty()) {
        error = "no triangle is selected";
        return RV_ERR_INVAL;
    }
    const std::string bin_name = gltf.stem().string() + std::string(RV_EDITOR_GLTF_BIN_EXT);
    if (!plain_uri(bin_name)) {
        error = "file name '" + bin_name + "' has '%', ':' or a leading '/', which the reader cannot open";
        return RV_ERR_INVAL;
    }
    const std::filesystem::path bin_path = gltf.parent_path() / bin_name;
    const int fresh = require_new(gltf, bin_path, error);
    if (fresh != RV_OK) {
        return fresh;
    }
    const int valid = rv_pdklib::rv_mppcmod_validate(model, error);
    if (valid != RV_OK) {
        return valid;
    }
    gltf_mesh mesh;
    const int picked = pick_runs(model, tris, mesh.runs, error);
    if (picked != RV_OK) {
        return picked;
    }
    build_mesh(model, mesh);

    std::vector<uint8_t> bin;
    build_bin(mesh, bin);
    std::string json;
    const int built = build_json(mesh, texture_uri, bin_name, json, error);
    if (built != RV_OK) {
        return built;
    }

    // The .bin goes first: a reader that sees the new .gltf finds its buffer.
    const std::string bin_text(reinterpret_cast<const char *>(bin.data()), bin.size());
    const int wrote_bin = rv_editor_file_replace(bin_path, bin_text, error);
    if (wrote_bin != RV_OK) {
        return wrote_bin;
    }
    const int wrote_gltf = rv_editor_file_replace(gltf, json, error);
    if (wrote_gltf != RV_OK) {
        // Without the .gltf the new .bin belongs to no model: remove it.
        std::error_code ec;
        std::filesystem::remove(bin_path, ec);
        if (ec) {
            error += "; cannot remove " + bin_path.string() + ": " + ec.message();
        }
        return wrote_gltf;
    }
    return RV_OK;
}

} // namespace rv_editor
