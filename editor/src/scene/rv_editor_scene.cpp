// Scene documents: pdklib's rv_scene reads and writes the file; this file maps it to the
// editor's model and back, so the editor keeps no parser of its own.

#include "scene/rv_editor_scene.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <random>

#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_scene/rv_scene.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

// Default names for newly created objects.
constexpr std::string_view default_camera_name = "Camera";
constexpr std::string_view default_mesh_name = "Box";

// Vector component count (x, y, z).
constexpr size_t vec3_size = 3;
// UV coordinate component count (u, v, u2, v2).
constexpr size_t uv_size = 4;

// Tint channel range (0-255 inclusive): the file's rv_color keeps one byte per channel.
constexpr int tint_min = 0;
constexpr int tint_max = 255;

// Initial camera properties in new scenes.
constexpr double initial_camera_y = 0.8;
constexpr double initial_camera_z = -4.0;
constexpr double initial_camera_rot_x = 8.0;

// Mask for random ID generation to ensure minimum value.
constexpr uint32_t id_generation_mask = 0x10000000u;
// Object ids are written in hexadecimal.
constexpr int id_hex_base = 16;
// Buffer for formatting random scene object ID as hexadecimal via to_chars; size for uint32_t.
constexpr size_t hex_id_buffer_bytes = 16;

// Default preamble for new scene files.
constexpr std::string_view scene_preamble = "# A scene: 3dmppc-editor's Scene layout edits it.\n\n";

// The model number of a raw Q20.12 value; exact, the file's units are 1/4096.
double rv_editor_scene_units(rv_pdklib::fx raw)
{
    return static_cast<double>(raw) / rv_pdklib::fx_one;
}

rv_editor_vec3 rv_editor_scene_vec3_units(const rv_pdklib::fx_vec3 &v)
{
    return { rv_editor_scene_units(v.x), rv_editor_scene_units(v.y), rv_editor_scene_units(v.z) };
}

// Q20.12 of a model number; RV_ERR_INVAL names the object and field when it does not fit.
int rv_editor_scene_fx(const std::string &object, const char *field, double value, rv_pdklib::fx &out, std::string &error)
{
    if (rv_pdklib::fx_from_double(value, out) != RV_OK) {
        error = "object '" + object + "': '" + field + "' is outside Q20.12";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

template <size_t count>
int rv_editor_scene_fx_list(const std::string &object,
    const char *field,
    const std::array<double, count> &in,
    std::array<rv_pdklib::fx, count> &out,
    std::string &error)
{
    for (size_t i = 0; i < count; ++i) {
        if (rv_editor_scene_fx(object, field, in[i], out[i], error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

int rv_editor_scene_fx_vec3(const std::string &object,
    const char *field,
    const rv_editor_vec3 &in,
    rv_pdklib::fx_vec3 &out,
    std::string &error)
{
    std::array<rv_pdklib::fx, vec3_size> q{};
    if (rv_editor_scene_fx_list(object, field, in, q, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = { q[0], q[1], q[2] };
    return RV_OK;
}

int rv_editor_scene_color(const rv_editor_scene_object &o, rv_color &out, std::string &error)
{
    for (int c : o.tint) {
        if (c < tint_min || c > tint_max) {
            error = "object '" + o.id + "': 'tint' must be three integers from 0 to 255";
            return RV_ERR_INVAL;
        }
    }
    out = { static_cast<uint8_t>(o.tint[0]), static_cast<uint8_t>(o.tint[1]), static_cast<uint8_t>(o.tint[2]) };
    return RV_OK;
}

// One object as pdklib's scene object; `error` names the object and field that do not fit Q20.12.
int rv_editor_scene_object_to_pdk(const rv_editor_scene &scene,
    const rv_editor_scene_object &o,
    rv_pdklib::rv_scene_object &p,
    std::string &error)
{
    std::array<rv_pdklib::fx, uv_size> uv{};
    if (rv_editor_scene_fx_vec3(o.id, "position", o.position, p.position, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_vec3(o.id, "rotation", o.rotation, p.rotation, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_vec3(o.id, "scale", o.scale, p.scale, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_list(o.id, "uv", o.uv, uv, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_color(o, p.tint, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx(o.id, "tess", o.tess, p.tess, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    // Checked after rounding: a tess that rounds to 0 would not read back.
    if (p.tess <= 0) {
        error = "object '" + o.id + "': 'tess' must be a number greater than zero";
        return RV_ERR_INVAL;
    }
    std::copy(uv.begin(), uv.end(), std::begin(p.uv));
    p.id = o.id;
    p.name = o.name;
    p.parent = rv_editor_scene_find(scene, o.parent);
    p.kind = o.kind;
    p.entity = o.entity;
    p.mesh = o.mesh;
    p.texture = o.texture;
    p.extra = o.extra;
    return RV_OK;
}

// The model as pdklib's scene. A component of a missing object gets index -1, which rv_scene_render skips.
int rv_editor_scene_to_pdk(const rv_editor_scene &scene, rv_pdklib::rv_scene &pdk, std::string &error)
{
    pdk.version_major = scene.version_major;
    pdk.version_minor = scene.version_minor;
    pdk.preamble = scene.preamble;
    pdk.scene_extra = scene.scene_extra;
    pdk.other_sections = scene.other_sections;
    for (const rv_editor_scene_object &o : scene.objects) {
        rv_pdklib::rv_scene_object p;
        if (rv_editor_scene_object_to_pdk(scene, o, p, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
        pdk.objects.push_back(std::move(p));
    }
    for (const rv_editor_scene_component &c : scene.components) {
        rv_pdklib::rv_scene_component p;
        p.object = rv_editor_scene_find(scene, c.object);
        p.name = c.name;
        p.fields = c.fields;
        pdk.components.push_back(std::move(p));
    }
    return RV_OK;
}

// One object of a parsed scene as the editor's model; its parent index is already in range.
rv_editor_scene_object rv_editor_scene_object_from_pdk(const rv_pdklib::rv_scene &pdk, const rv_pdklib::rv_scene_object &p)
{
    rv_editor_scene_object o;
    o.id = p.id;
    o.name = p.name;
    o.parent = p.parent < 0 ? std::string() : pdk.objects[static_cast<size_t>(p.parent)].id;
    o.kind = p.kind;
    o.entity = p.entity;
    o.position = rv_editor_scene_vec3_units(p.position);
    o.rotation = rv_editor_scene_vec3_units(p.rotation);
    o.scale = rv_editor_scene_vec3_units(p.scale);
    for (size_t i = 0; i < uv_size; ++i) {
        o.uv[i] = rv_editor_scene_units(p.uv[i]);
    }
    o.tint = { p.tint.r, p.tint.g, p.tint.b };
    o.tess = rv_editor_scene_units(p.tess);
    o.mesh = p.mesh;
    o.texture = p.texture;
    o.extra = p.extra;
    return o;
}

} // namespace

int rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error)
{
    // An unreadable file gives an empty text, which would otherwise report a missing version.
    if (!std::ifstream(path, std::ios::binary)) {
        error = path.string() + ": cannot read";
        return RV_ERR_INVAL;
    }
    rv_pdklib::rv_scene pdk;
    if (rv_pdklib::rv_scene_parse(rv_editor_file_text(path), path.string(), pdk, error) != 0) {
        return RV_ERR_INVAL;
    }
    rv_editor_scene s;
    s.path = path;
    s.version_major = pdk.version_major;
    s.version_minor = pdk.version_minor;
    s.preamble = pdk.preamble;
    s.scene_extra = pdk.scene_extra;
    s.other_sections = pdk.other_sections;
    for (const rv_pdklib::rv_scene_object &p : pdk.objects) {
        s.objects.push_back(rv_editor_scene_object_from_pdk(pdk, p));
    }
    for (const rv_pdklib::rv_scene_component &p : pdk.components) {
        rv_editor_scene_component c;
        c.object = pdk.objects[static_cast<size_t>(p.object)].id;
        c.name = p.name;
        c.fields = p.fields;
        s.components.push_back(std::move(c));
    }
    scene = std::move(s);
    return RV_OK;
}

int rv_editor_scene_save(const rv_editor_scene &scene, std::string &error)
{
    if (!scene.read_only.empty()) {
        error = "read-only: " + scene.read_only;
        return RV_ERR_INVAL;
    }
    rv_pdklib::rv_scene pdk;
    if (rv_editor_scene_to_pdk(scene, pdk, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const int code = rv_editor_file_replace(scene.path, rv_pdklib::rv_scene_render(pdk), error);
    if (code != RV_OK) {
        return code;
    }
    return RV_OK;
}

rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path)
{
    rv_editor_scene s;
    s.path = path;
    s.preamble = std::string(scene_preamble);
    rv_editor_scene_object camera;
    camera.id = rv_editor_scene_new_id(s);
    camera.name = std::string(default_camera_name);
    camera.kind = std::string(kind_camera);
    camera.position = { 0.0, initial_camera_y, initial_camera_z };
    camera.rotation = { initial_camera_rot_x, 0.0, 0.0 };
    s.objects.push_back(camera);
    rv_editor_scene_object box;
    box.id = rv_editor_scene_new_id(s);
    box.name = std::string(default_mesh_name);
    box.kind = std::string(kind_mesh);
    s.objects.push_back(box);
    return s;
}

std::string rv_editor_scene_new_id(const rv_editor_scene &scene)
{
    static std::mt19937 gen{ std::random_device{}() };
    for (;;) {
        char buf[hex_id_buffer_bytes];
        const auto r = std::to_chars(buf, buf + sizeof(buf), gen() | id_generation_mask, id_hex_base);
        const std::string id(buf, r.ptr);
        if (rv_editor_scene_find(scene, id) < 0) {
            return id;
        }
    }
}

int rv_editor_scene_find(const rv_editor_scene &scene, const std::string &id)
{
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        if (scene.objects[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace rv_editor
