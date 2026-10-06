// Scene documents: read with pdklib's dialect, written back whole with the keys
// the editor does not know, never rewritten when their format is newer.

#include "scene/rv_editor_scene.hpp"

#include <charconv>
#include <random>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

using kind = rv_pdklib::rv_manifest_value_kind;

// Scene file section names.
constexpr std::string_view scene_section_key = "scene";
constexpr std::string_view object_section_key = "object";

// Scene section field keys.
constexpr std::string_view scene_version_key = "version";
constexpr std::string_view scene_format_key = "format";

// Object field keys.
constexpr std::string_view object_id_key = "id";
constexpr std::string_view object_name_key = "name";
constexpr std::string_view object_parent_key = "parent";
constexpr std::string_view object_kind_key = "kind";
constexpr std::string_view object_mesh_key = "mesh";
constexpr std::string_view object_texture_key = "texture";
constexpr std::string_view object_position_key = "position";
constexpr std::string_view object_rotation_key = "rotation";
constexpr std::string_view object_scale_key = "scale";
constexpr std::string_view object_uv_key = "uv";
constexpr std::string_view object_tint_key = "tint";
constexpr std::string_view object_tess_key = "tess";

// Scene object kinds.
constexpr std::string_view kind_camera = "camera";
constexpr std::string_view kind_mesh = "mesh";
constexpr std::string_view kind_quad = "quad";
constexpr std::string_view kind_billboard = "billboard";
constexpr std::string_view kind_volume = "volume";

// Default names for newly created objects.
constexpr std::string_view default_camera_name = "Camera";
constexpr std::string_view default_mesh_name = "Box";

// Vector component count (x, y, z).
constexpr size_t vec3_size = 3;
// Space axes: x, y, z.
constexpr int space_axes = 3;
// UV coordinate component count (u, v, u2, v2).
constexpr size_t uv_size = 4;

// Tint value range (0-255 inclusive).
constexpr double tint_min = 0.0;
constexpr double tint_max = 255.0;
// Default tint value for all channels.
constexpr int default_tint_value = 255;

// Default property values.
constexpr double default_uv_value = 0.0;
constexpr double default_tess_value = 2.0;

// Initial camera properties in new scenes.
constexpr double initial_camera_y = 0.8;
constexpr double initial_camera_z = -4.0;
constexpr double initial_camera_rot_x = 8.0;

// Version that requires new properties (quad, billboard, volume, uv, tint, tess).
constexpr uint32_t min_new_features_minor = 4;

// Mask for random ID generation to ensure minimum value.
constexpr uint32_t id_generation_mask = 0x10000000u;
// Object ids are written in hexadecimal.
constexpr int id_hex_base = 16;

// TOML format delimiters.
constexpr std::string_view array_start = "[";
constexpr std::string_view array_end = "]";
constexpr std::string_view array_sep = ", ";
constexpr std::string_view entry_sep = " = ";
constexpr std::string_view array_section_start = "[[";
constexpr std::string_view array_section_end = "]]";
constexpr std::string_view section_start = "[";
constexpr std::string_view section_end = "]";
constexpr std::string_view section_header_scene = "[scene]";
constexpr std::string_view section_header_object = "[[object]]";

// Default preamble for new scene files.
constexpr std::string_view scene_preamble = "# A scene: 3dmppc-editor's Scene layout edits it.\n\n";

// Shortest text that reads back as the same double, never in exponent form (the
// dialect has none): 1, -0.25, 1.5.
std::string rv_editor_scene_number(double v)
{
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed);
    return std::string(buf, r.ptr);
}

std::string rv_editor_scene_vec(const rv_editor_vec3 &v)
{
    return std::string(array_start) + rv_editor_scene_number(v[0]) + std::string(array_sep) +
        rv_editor_scene_number(v[1]) + std::string(array_sep) + rv_editor_scene_number(v[2]) +
        std::string(array_end);
}

std::string rv_editor_scene_value(const rv_pdklib::rv_manifest_mvalue &v)
{
    switch (v.kind) {
        case kind::string: return rv_editor_toml_quote(v.str);
        case kind::integer: return std::to_string(v.num);
        case kind::real: return rv_editor_scene_number(v.real);
        case kind::array: {
            std::string out(array_start);
            for (size_t i = 0; i < v.arr.size(); ++i) {
                out += (i == 0 ? "" : std::string(array_sep)) + rv_editor_toml_quote(v.arr[i]);
            }
            return out + std::string(array_end);
        }
        case kind::numbers: {
            std::string out(array_start);
            for (size_t i = 0; i < v.nums.size(); ++i) {
                out += (i == 0 ? "" : std::string(array_sep)) + rv_editor_scene_number(v.nums[i]);
            }
            return out + std::string(array_end);
        }
    }
    return "\"\"";
}

void rv_editor_scene_entries(std::string &t, const std::vector<rv_pdklib::rv_manifest_tree_entry> &entries)
{
    for (const auto &e : entries) {
        t += e.key + std::string(entry_sep) + rv_editor_scene_value(e.value) + "\n";
    }
}

// A malformed uv/tint/tess sits in `extra` as read; skip the defaulted field so
// the key is not written twice.
bool rv_editor_scene_extra_has(const std::vector<rv_pdklib::rv_manifest_tree_entry> &extra, const std::string &key)
{
    for (const auto &e : extra) {
        if (e.key == key) {
            return true;
        }
    }
    return false;
}

int rv_editor_scene_vec_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_vec3 &out)
{
    if (v.kind != kind::numbers || v.nums.size() != vec3_size) {
        return RV_ERR_INVAL;
    }
    out = { v.nums[0], v.nums[1], v.nums[2] };
    return RV_OK;
}

int rv_editor_scene_uv_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_uv &out)
{
    if (v.kind != kind::numbers || v.nums.size() != uv_size) {
        return RV_ERR_INVAL;
    }
    out = { v.nums[0], v.nums[1], v.nums[2], v.nums[3] };
    return RV_OK;
}

int rv_editor_scene_tint_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_tint &out)
{
    if (v.kind != kind::numbers || v.nums.size() != vec3_size) {
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < space_axes; ++i) {
        const double n = v.nums[static_cast<size_t>(i)];
        if (n < tint_min || n > tint_max || n != static_cast<double>(static_cast<int>(n))) {
            return RV_ERR_INVAL;
        }
    }
    out = { static_cast<int>(v.nums[0]), static_cast<int>(v.nums[1]), static_cast<int>(v.nums[2]) };
    return RV_OK;
}

int rv_editor_scene_tess_of(const rv_pdklib::rv_manifest_mvalue &v, double &out)
{
    double n;
    if (v.kind == kind::integer) {
        n = static_cast<double>(v.num);
    } else if (v.kind == kind::real) {
        n = v.real;
    } else {
        return RV_ERR_INVAL;
    }
    if (n <= 0.0) {
        return RV_ERR_INVAL;
    }
    out = n;
    return RV_OK;
}

} // namespace

int rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error)
{
    const std::string text = rv_editor_file_text(path);
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text, path.string(), tree, error) != 0) {
        return RV_ERR_INVAL;
    }
    rv_editor_scene s;
    s.path = path;
    bool has_version = false;
    bool has_legacy_format = false;
    std::string version_str;
    // The comment block above the first section, which the template uses to say what the file is.
    for (size_t at = 0; at < text.size() && text[at] != '[';) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string::npos ? text.size() : nl + 1;
        s.preamble += text.substr(at, end - at);
        at = end;
    }
    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        if (section.name == scene_section_key && !section.array) {
            for (const auto &e : section.entries) {
                if (e.key == scene_version_key && e.value.kind == kind::string) {
                    version_str = e.value.str;
                    has_version = true;
                } else if (e.key == scene_format_key && e.value.kind == kind::integer) {
                    has_legacy_format = true;
                } else {
                    s.scene_extra.push_back(e);
                }
            }
            continue;
        }
        if (section.name != object_section_key || !section.array) {
            s.other_sections.push_back(section);
            continue;
        }
        rv_editor_scene_object o;
        for (const auto &e : section.entries) {
            std::string *field = e.key == object_id_key ? &o.id : e.key == object_name_key ? &o.name :
                e.key == object_parent_key                                                 ? &o.parent :
                e.key == object_kind_key                                                   ? &o.kind :
                e.key == object_mesh_key                                                   ? &o.mesh :
                e.key == object_texture_key                                                ? &o.texture :
                                                                                             nullptr;
            rv_editor_vec3 *vec = e.key == object_position_key ? &o.position : e.key == object_rotation_key ? &o.rotation :
                e.key == object_scale_key                                                                   ? &o.scale :
                                                                                                              nullptr;
            // A known key of an unexpected shape is kept as it was, not overwritten.
            if (field != nullptr && e.value.kind == kind::string) {
                *field = e.value.str;
            } else if (field != nullptr || vec != nullptr) {
                if (vec != nullptr && rv_editor_scene_vec_of(e.value, *vec) == RV_OK) {
                    continue;
                }
                o.extra.push_back(e);
                s.read_only = "object '" + o.id + "': '" + e.key + "' is not what this editor writes";
            } else if (e.key == object_uv_key) {
                if (rv_editor_scene_uv_of(e.value, o.uv) != RV_OK) {
                    o.extra.push_back(e);
                    s.read_only = "object '" + o.id + "': 'uv' must be four numbers";
                }
            } else if (e.key == object_tint_key) {
                if (rv_editor_scene_tint_of(e.value, o.tint) != RV_OK) {
                    o.extra.push_back(e);
                    s.read_only = "object '" + o.id + "': 'tint' must be three integers from 0 to 255";
                }
            } else if (e.key == object_tess_key) {
                if (rv_editor_scene_tess_of(e.value, o.tess) != RV_OK) {
                    o.extra.push_back(e);
                    s.read_only = "object '" + o.id + "': 'tess' must be a number greater than zero";
                }
            } else {
                o.extra.push_back(e);
            }
        }
        s.objects.push_back(std::move(o));
    }
    if (has_version) {
        uint32_t major = 0, minor = 0;
        if (rv_pdklib::rv_version_parse(version_str, major, minor) != RV_OK) {
            error = path.string() + ": malformed [scene] version '" + version_str + "'";
            return RV_ERR_INVAL;
        }
        s.version_major = major;
        s.version_minor = minor;
    } else if (has_legacy_format) {
        s.version_major = 0;
        s.version_minor = 0;
    } else {
        error = path.string() + ": no [scene] version";
        return RV_ERR_INVAL;
    }
    if (!rv_pdklib::rv_version_compatible(s.version_major, s.version_minor)) {
        s.read_only = "scene version " + std::to_string(s.version_major) + "." + std::to_string(s.version_minor) +
            " is not compatible with this editor's " + rv_pdklib::rv_version_str;
    }
    scene = std::move(s);
    return RV_OK;
}

std::string rv_editor_scene_render(const rv_editor_scene &scene)
{
    std::string t = scene.preamble;
    if (!t.empty() && t.back() != '\n') {
        t += '\n';
    }
    // Keep the file's own version; a legacy 0.0 header is upgraded to the current one.
    // Content with quad/billboard/volume or uv/tint/tess keys requires at least 0.4.
    const bool legacy = scene.version_major == 0 && scene.version_minor == 0;
    uint32_t version_major = scene.version_major;
    uint32_t version_minor = scene.version_minor;

    if (legacy) {
        // Upgrade legacy 0.0 to current version
        version_major = static_cast<uint32_t>(RV_MPPC_VER_MAJOR);
        version_minor = static_cast<uint32_t>(RV_MPPC_VER_MINOR);
    } else {
        // Check if content requires 0.4: quad, billboard, volume, or uv/tint/tess keys
        bool needs_v04 = false;
        for (const auto &obj : scene.objects) {
            if (obj.kind == kind_quad || obj.kind == kind_billboard || obj.kind == kind_volume) {
                needs_v04 = true;
                break;
            }
            // Check for non-default uv/tint/tess
            if (obj.uv[0] != default_uv_value || obj.uv[1] != default_uv_value ||
                obj.uv[2] != default_uv_value || obj.uv[3] != default_uv_value) {
                needs_v04 = true;
                break;
            }
            if (obj.tint[0] != default_tint_value || obj.tint[1] != default_tint_value ||
                obj.tint[2] != default_tint_value) {
                needs_v04 = true;
                break;
            }
            if (obj.tess != default_tess_value) {
                needs_v04 = true;
                break;
            }
            // Check if extra has uv/tint/tess keys
            if (rv_editor_scene_extra_has(obj.extra, std::string(object_uv_key)) ||
                rv_editor_scene_extra_has(obj.extra, std::string(object_tint_key)) ||
                rv_editor_scene_extra_has(obj.extra, std::string(object_tess_key))) {
                needs_v04 = true;
                break;
            }
        }

        // Upgrade to 0.4 if needed, but never downgrade
        if (needs_v04 && version_major == 0 && version_minor < min_new_features_minor) {
            version_major = 0;
            version_minor = min_new_features_minor;
        }
    }

    std::string version = std::to_string(version_major) + "." + std::to_string(version_minor);
    t += std::string(section_header_scene) + "\n" + std::string(scene_version_key) + std::string(entry_sep) +
        rv_editor_toml_quote(version) + "\n";
    rv_editor_scene_entries(t, scene.scene_extra);
    for (const rv_editor_scene_object &o : scene.objects) {
        t += "\n" + std::string(section_header_object) + "\n";
        t += std::string(object_id_key) + std::string(entry_sep) + rv_editor_toml_quote(o.id) + "\n";
        t += std::string(object_name_key) + std::string(entry_sep) + rv_editor_toml_quote(o.name) + "\n";
        t += std::string(object_parent_key) + std::string(entry_sep) + rv_editor_toml_quote(o.parent) + "\n";
        t += std::string(object_kind_key) + std::string(entry_sep) + rv_editor_toml_quote(o.kind) + "\n";
        t += std::string(object_position_key) + std::string(entry_sep) + rv_editor_scene_vec(o.position) + "\n";
        t += std::string(object_rotation_key) + std::string(entry_sep) + rv_editor_scene_vec(o.rotation) + "\n";
        t += std::string(object_scale_key) + std::string(entry_sep) + rv_editor_scene_vec(o.scale) + "\n";
        t += std::string(object_mesh_key) + std::string(entry_sep) + rv_editor_toml_quote(o.mesh) + "\n";
        t += std::string(object_texture_key) + std::string(entry_sep) + rv_editor_toml_quote(o.texture) + "\n";
        if (o.kind == kind_quad || o.kind == kind_billboard) {
            if (!rv_editor_scene_extra_has(o.extra, std::string(object_uv_key))) {
                t += std::string(object_uv_key) + std::string(entry_sep) + std::string(array_start) +
                    rv_editor_scene_number(o.uv[0]) + std::string(array_sep) + rv_editor_scene_number(o.uv[1]) +
                    std::string(array_sep) + rv_editor_scene_number(o.uv[2]) + std::string(array_sep) +
                    rv_editor_scene_number(o.uv[3]) + std::string(array_end) + "\n";
            }
            if (!rv_editor_scene_extra_has(o.extra, std::string(object_tint_key))) {
                t += std::string(object_tint_key) + std::string(entry_sep) + std::string(array_start) +
                    std::to_string(o.tint[0]) + std::string(array_sep) + std::to_string(o.tint[1]) +
                    std::string(array_sep) + std::to_string(o.tint[2]) + std::string(array_end) + "\n";
            }
            if (!rv_editor_scene_extra_has(o.extra, std::string(object_tess_key))) {
                t += std::string(object_tess_key) + std::string(entry_sep) + rv_editor_scene_number(o.tess) + "\n";
            }
        }
        rv_editor_scene_entries(t, o.extra);
    }
    for (const auto &section : scene.other_sections) {
        t += "\n" + (section.array ? std::string(array_section_start) : std::string(section_start)) + section.name +
            (section.array ? std::string(array_section_end) : std::string(section_end)) + "\n";
        rv_editor_scene_entries(t, section.entries);
    }
    return t;
}

int rv_editor_scene_save(const rv_editor_scene &scene, std::string &error)
{
    if (!scene.read_only.empty()) {
        error = "read-only: " + scene.read_only;
        return RV_ERR_INVAL;
    }
    const int code = rv_editor_file_replace(scene.path, rv_editor_scene_render(scene), error);
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
        char buf[16];
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
