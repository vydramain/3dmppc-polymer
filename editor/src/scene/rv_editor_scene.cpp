// Scene documents: pdkkit's rv_scene reads and writes the file; this file maps it to the
// editor's model and back, so the editor keeps no parser of its own.

#include "scene/rv_editor_scene.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <random>

#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "project/rv_editor_toml.hpp"
#include "text/rv_editor_text.hpp"

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

// Initial camera properties in new scenes, raw Q20.12: y 0.8 (rounded to 1/4096), z -4.0, rot x 8.0 degrees.
constexpr int64_t initial_camera_y = 3277;
constexpr int64_t initial_camera_z = -4 * rv_pdklib::fx_one;
constexpr int64_t initial_camera_rot_x = 8 * rv_pdklib::fx_one;

// Limit of a raw value: far outside Q20.12, so the save refuses it with the object's and field's name, as it refused
// such a double before. A non-finite number saturates to it too (NaN to plus).
constexpr int64_t object_raw_limit = int64_t{ 1 } << 62;

// Mask for random ID generation to ensure minimum value.
constexpr uint32_t id_generation_mask = 0x10000000u;
// Object ids are written in hexadecimal.
constexpr int id_hex_base = 16;
// Buffer for formatting random scene object ID as hexadecimal via to_chars; size for uint32_t.
constexpr size_t hex_id_buffer_bytes = 16;

// Default preamble for new scene files.
constexpr std::string_view scene_preamble = "# A scene: 3dmppc-editor's Scene layout edits it.\n\n";

// Model number of a raw Q20.12 value; exact, the file's units are 1/4096.
double rv_editor_scene_units(int64_t raw)
{
    return static_cast<double>(raw) / rv_pdklib::fx_one;
}

// Model numbers of raw Q20.12 values.
template <size_t count>
std::array<double, count> rv_editor_scene_units_list(const std::array<int64_t, count> &raw)
{
    std::array<double, count> out{};
    for (size_t i = 0; i < count; ++i) {
        out[i] = rv_editor_scene_units(raw[i]);
    }
    return out;
}

// Raw Q20.12 of a model number: nearest, half away from zero, as rv_pdklib::fx_from_double rounds.
// NaN and values at or beyond the limit saturate; the save refuses them.
int64_t rv_editor_scene_raw_from_units(double value)
{
    const double raw = std::round(value * rv_pdklib::fx_one);
    const double limit = static_cast<double>(object_raw_limit);
    if (std::isnan(raw) || raw >= limit) {
        return object_raw_limit;
    }
    if (raw <= -limit) {
        return -object_raw_limit;
    }
    return static_cast<int64_t>(raw);
}

template <size_t count>
std::array<int64_t, count> rv_editor_scene_raw_list(const std::array<double, count> &in)
{
    std::array<int64_t, count> out{};
    for (size_t i = 0; i < count; ++i) {
        out[i] = rv_editor_scene_raw_from_units(in[i]);
    }
    return out;
}

// Q20.12 of a raw value; RV_ERR_INVAL names the object and field when it does not fit.
int rv_editor_scene_fx(const std::string &object, const char *field, int64_t raw, rv_pdklib::fx &out, std::string &error)
{
    if (rv_pdklib::fx_from_raw(raw, out) != RV_OK) {
        error = "object '" + object + "': '" + field + "' is outside Q20.12";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

template <size_t count>
int rv_editor_scene_fx_list(const std::string &object,
    const char *field,
    const std::array<int64_t, count> &in,
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
    const rv_editor_vec3_raw &in,
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

// One object as pdkkit's scene object; `error` names the object and field that do not fit Q20.12.
int rv_editor_scene_object_to_pdk(const rv_editor_scene &scene,
    const rv_editor_scene_object &o,
    rv_pdkkit::rv_scene_object &p,
    std::string &error)
{
    std::array<rv_pdklib::fx, uv_size> uv{};
    if (rv_editor_scene_fx_vec3(o.id, "position", o.position_raw, p.position, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_vec3(o.id, "rotation", o.rotation_raw, p.rotation, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_vec3(o.id, "scale", o.scale_raw, p.scale, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx_list(o.id, "uv", o.uv_raw, uv, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_color(o, p.tint, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_editor_scene_fx(o.id, "tess", o.tess_raw, p.tess, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    // Checked after the range test: a tess outside Q20.12 keeps its range error.
    if (o.tess_raw <= 0) {
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

// The model as pdkkit's scene: each object's components follow the object, in object order.
int rv_editor_scene_to_pdk(const rv_editor_scene &scene, rv_pdkkit::rv_scene &pdk, std::string &error)
{
    pdk.version_major = scene.version_major;
    pdk.version_minor = scene.version_minor;
    pdk.preamble = scene.preamble;
    pdk.scene_extra = scene.scene_extra;
    pdk.other_sections = scene.other_sections;
    for (const rv_editor_scene_object &o : scene.objects) {
        rv_pdkkit::rv_scene_object p;
        if (rv_editor_scene_object_to_pdk(scene, o, p, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
        pdk.objects.push_back(std::move(p));
    }
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        for (const rv_editor_scene_component &c : scene.objects[i].components) {
            rv_pdkkit::rv_scene_component p;
            p.object = static_cast<int32_t>(i);
            p.name = c.name;
            p.fields = c.fields;
            pdk.components.push_back(std::move(p));
        }
    }
    return RV_OK;
}

// One object of a parsed scene as the editor's model; its parent index is already in range.
rv_editor_scene_object rv_editor_scene_object_from_pdk(const rv_pdkkit::rv_scene &pdk, const rv_pdkkit::rv_scene_object &p)
{
    rv_editor_scene_object o;
    o.id = p.id;
    o.name = p.name;
    o.parent = p.parent < 0 ? std::string() : pdk.objects[static_cast<size_t>(p.parent)].id;
    o.kind = p.kind;
    o.entity = p.entity;
    o.position_raw = { p.position.x, p.position.y, p.position.z };
    o.rotation_raw = { p.rotation.x, p.rotation.y, p.rotation.z };
    o.scale_raw = { p.scale.x, p.scale.y, p.scale.z };
    for (size_t i = 0; i < uv_size; ++i) {
        o.uv_raw[i] = p.uv[i];
    }
    o.tint = { p.tint.r, p.tint.g, p.tint.b };
    o.tess_raw = p.tess;
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
    rv_pdkkit::rv_scene pdk;
    if (rv_pdkkit::rv_scene_parse(rv_editor_file_text(path), path.string(), pdk, error) != 0) {
        return RV_ERR_INVAL;
    }
    rv_editor_scene s;
    s.path = path;
    s.version_major = pdk.version_major;
    s.version_minor = pdk.version_minor;
    s.preamble = pdk.preamble;
    s.scene_extra = pdk.scene_extra;
    s.other_sections = pdk.other_sections;
    for (const rv_pdkkit::rv_scene_object &p : pdk.objects) {
        s.objects.push_back(rv_editor_scene_object_from_pdk(pdk, p));
    }
    for (const rv_pdkkit::rv_scene_component &p : pdk.components) {
        rv_editor_scene_component c;
        c.name = p.name;
        c.fields = p.fields;
        s.objects[static_cast<size_t>(p.object)].components.push_back(std::move(c));
    }
    // An object with no kind is in the component form, which the editor does not edit yet: the scene opens read-only.
    const bool has_component_form = std::any_of(s.objects.begin(), s.objects.end(), [](const rv_editor_scene_object &o) {
        return o.kind.empty();
    });
    s.read_only = has_component_form ? std::string(rv_editor_text("pane_scene.component_form_read_only")) : std::string();
    scene = std::move(s);
    return RV_OK;
}

int rv_editor_scene_save(const rv_editor_scene &scene, std::string &error)
{
    if (!scene.read_only.empty()) {
        error = "read-only: " + scene.read_only;
        return RV_ERR_INVAL;
    }
    rv_pdkkit::rv_scene pdk;
    if (rv_editor_scene_to_pdk(scene, pdk, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const int code = rv_editor_file_replace(scene.path, rv_pdkkit::rv_scene_render(pdk), error);
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
    camera.position_raw = { 0, initial_camera_y, initial_camera_z };
    camera.rotation_raw = { initial_camera_rot_x, 0, 0 };
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

const rv_editor_scene_component *rv_editor_scene_component_find(const rv_editor_scene_object &o, std::string_view name)
{
    for (const rv_editor_scene_component &c : o.components) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

bool rv_editor_scene_has_component(const rv_editor_scene_object &o, std::string_view name)
{
    return rv_editor_scene_component_find(o, name) != nullptr;
}

std::string rv_editor_object_get_name(const rv_editor_scene_object &o)
{
    return o.name;
}

void rv_editor_object_set_name(rv_editor_scene_object &o, const std::string &value)
{
    o.name = value;
}

std::string rv_editor_object_get_kind(const rv_editor_scene_object &o)
{
    return o.kind;
}

void rv_editor_object_set_kind(rv_editor_scene_object &o, const std::string &value)
{
    o.kind = value;
}

rv_editor_vec3 rv_editor_object_get_position(const rv_editor_scene_object &o)
{
    return rv_editor_scene_units_list(o.position_raw);
}

void rv_editor_object_set_position(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    o.position_raw = rv_editor_scene_raw_list(value);
}

rv_editor_vec3 rv_editor_object_get_rotation(const rv_editor_scene_object &o)
{
    return rv_editor_scene_units_list(o.rotation_raw);
}

void rv_editor_object_set_rotation(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    o.rotation_raw = rv_editor_scene_raw_list(value);
}

rv_editor_vec3 rv_editor_object_get_scale(const rv_editor_scene_object &o)
{
    return rv_editor_scene_units_list(o.scale_raw);
}

void rv_editor_object_set_scale(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    o.scale_raw = rv_editor_scene_raw_list(value);
}

std::string rv_editor_object_get_mesh(const rv_editor_scene_object &o)
{
    return o.mesh;
}

void rv_editor_object_set_mesh(rv_editor_scene_object &o, const std::string &value)
{
    o.mesh = value;
}

std::string rv_editor_object_get_texture(const rv_editor_scene_object &o)
{
    return o.texture;
}

void rv_editor_object_set_texture(rv_editor_scene_object &o, const std::string &value)
{
    o.texture = value;
}

rv_editor_uv rv_editor_object_get_uv(const rv_editor_scene_object &o)
{
    return rv_editor_scene_units_list(o.uv_raw);
}

void rv_editor_object_set_uv(rv_editor_scene_object &o, const rv_editor_uv &value)
{
    o.uv_raw = rv_editor_scene_raw_list(value);
}

rv_editor_tint rv_editor_object_get_tint(const rv_editor_scene_object &o)
{
    return o.tint;
}

void rv_editor_object_set_tint(rv_editor_scene_object &o, const rv_editor_tint &value)
{
    o.tint = value;
}

double rv_editor_object_get_tess(const rv_editor_scene_object &o)
{
    return rv_editor_scene_units(o.tess_raw);
}

void rv_editor_object_set_tess(rv_editor_scene_object &o, double value)
{
    o.tess_raw = rv_editor_scene_raw_from_units(value);
}

} // namespace rv_editor
