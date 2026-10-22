// Scene documents: pdkkit's rv_scene reads and writes the file; this file maps it to the
// editor's model and back, so the editor keeps no parser of its own.

#include "scene/rv_editor_scene.hpp"

#include <charconv>
#include <fstream>
#include <random>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/scene/rv_scene_migrate.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

// Default names for newly created objects.
constexpr std::string_view default_camera_name = "Camera";
constexpr std::string_view default_mesh_name = "Box";

// Initial camera properties in new scenes: y 0.8 (the setter rounds it to 1/4096), z -4.0, rot x 8.0 degrees.
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

// Logs each line of a migration's notes (the fields it dropped) as a warning.
void log_lines(std::string_view text)
{
    while (!text.empty()) {
        const size_t end = text.find('\n');
        RV_LOG_WARN(scene_log_tag, "{}", text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }
}

// One object of a parsed scene as the editor's model; its parent index is already in range.
rv_editor_scene_object rv_editor_scene_object_from_pdk(const rv_pdkkit::rv_scene &pdk, const rv_pdkkit::rv_scene_object &p)
{
    rv_editor_scene_object o;
    o.id = p.id;
    o.parent = p.parent < 0 ? std::string() : pdk.objects[static_cast<size_t>(p.parent)].id;
    o.entity = p.entity;
    o.extra = p.extra;
    return o;
}

// The model as pdkkit's form-2 scene: each object by id, parent index, entity and extra; its components follow
// the object, in object order.
void rv_editor_scene_to_pdk(const rv_editor_scene &scene, rv_pdkkit::rv_scene &pdk)
{
    pdk.version_major = scene.version_major;
    pdk.version_minor = scene.version_minor;
    pdk.preamble = scene.preamble;
    pdk.scene_extra = scene.scene_extra;
    pdk.other_sections = scene.other_sections;
    for (const rv_editor_scene_object &o : scene.objects) {
        rv_pdkkit::rv_scene_object p;
        p.id = o.id;
        p.parent = rv_editor_scene_find(scene, o.parent);
        p.entity = o.entity;
        p.extra = o.extra;
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
    std::string notes;
    if (rv_pdkkit::rv_scene_migrate(pdk, notes) != RV_OK) {
        error = notes;
        return RV_ERR_INVAL;
    }
    log_lines(notes);
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
    for (const rv_editor_scene_object &o : s.objects) {
        if (rv_editor_scene_object_check(o, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
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
    for (const rv_editor_scene_object &o : scene.objects) {
        if (rv_editor_scene_object_check(o, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    rv_pdkkit::rv_scene pdk;
    rv_editor_scene_to_pdk(scene, pdk);
    return rv_editor_file_replace(scene.path, rv_pdkkit::rv_scene_render(pdk), error);
}

rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path)
{
    rv_editor_scene s;
    s.path = path;
    s.preamble = std::string(scene_preamble);
    rv_editor_scene_object camera;
    camera.id = rv_editor_scene_new_id(s);
    rv_editor_object_set_name(camera, std::string(default_camera_name));
    rv_editor_object_set_position(camera, { 0, initial_camera_y, initial_camera_z });
    rv_editor_object_set_rotation(camera, { initial_camera_rot_x, 0, 0 });
    rv_editor_object_set_kind(camera, std::string(kind_camera));
    s.objects.push_back(camera);
    // No model yet: an entity without one is shown as a box (plan 4.7, E10f).
    rv_editor_scene_object box;
    box.id = rv_editor_scene_new_id(s);
    rv_editor_object_set_name(box, std::string(default_mesh_name));
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

} // namespace rv_editor
