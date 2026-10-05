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
    return "[" + rv_editor_scene_number(v[0]) + ", " + rv_editor_scene_number(v[1]) + ", " +
        rv_editor_scene_number(v[2]) + "]";
}

std::string rv_editor_scene_value(const rv_pdklib::rv_manifest_mvalue &v)
{
    switch (v.kind) {
        case kind::string: return rv_editor_toml_quote(v.str);
        case kind::integer: return std::to_string(v.num);
        case kind::real: return rv_editor_scene_number(v.real);
        case kind::array: {
            std::string out = "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                out += (i == 0 ? "" : ", ") + rv_editor_toml_quote(v.arr[i]);
            }
            return out + "]";
        }
        case kind::numbers: {
            std::string out = "[";
            for (size_t i = 0; i < v.nums.size(); ++i) {
                out += (i == 0 ? "" : ", ") + rv_editor_scene_number(v.nums[i]);
            }
            return out + "]";
        }
    }
    return "\"\"";
}

void rv_editor_scene_entries(std::string &t, const std::vector<rv_pdklib::rv_manifest_tree_entry> &entries)
{
    for (const auto &e : entries) {
        t += e.key + " = " + rv_editor_scene_value(e.value) + "\n";
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
    if (v.kind != kind::numbers || v.nums.size() != 3) {
        return RV_ERR_INVAL;
    }
    out = { v.nums[0], v.nums[1], v.nums[2] };
    return RV_OK;
}

int rv_editor_scene_uv_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_uv &out)
{
    if (v.kind != kind::numbers || v.nums.size() != 4) {
        return RV_ERR_INVAL;
    }
    out = { v.nums[0], v.nums[1], v.nums[2], v.nums[3] };
    return RV_OK;
}

int rv_editor_scene_tint_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_tint &out)
{
    if (v.kind != kind::numbers || v.nums.size() != 3) {
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < 3; ++i) {
        const double n = v.nums[static_cast<size_t>(i)];
        if (n < 0.0 || n > 255.0 || n != static_cast<double>(static_cast<int>(n))) {
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

bool rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error)
{
    const std::string text = rv_editor_file_text(path);
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text, path.string(), tree, error) != 0) {
        return false;
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
        if (section.name == "scene" && !section.array) {
            for (const auto &e : section.entries) {
                if (e.key == "version" && e.value.kind == kind::string) {
                    version_str = e.value.str;
                    has_version = true;
                } else if (e.key == "format" && e.value.kind == kind::integer) {
                    has_legacy_format = true;
                } else {
                    s.scene_extra.push_back(e);
                }
            }
            continue;
        }
        if (section.name != "object" || !section.array) {
            s.other_sections.push_back(section);
            continue;
        }
        rv_editor_scene_object o;
        for (const auto &e : section.entries) {
            std::string *field = e.key == "id" ? &o.id : e.key == "name" ? &o.name : e.key == "parent" ? &o.parent
                : e.key == "kind"                                                              ? &o.kind
                : e.key == "mesh"                                                              ? &o.mesh
                : e.key == "texture"                                                           ? &o.texture
                                                                                               : nullptr;
            rv_editor_vec3 *vec = e.key == "position" ? &o.position : e.key == "rotation" ? &o.rotation
                : e.key == "scale"                                                          ? &o.scale
                                                                                            : nullptr;
            // A known key of an unexpected shape is kept as it was, not overwritten.
            if (field != nullptr && e.value.kind == kind::string) {
                *field = e.value.str;
            } else if (field != nullptr || vec != nullptr) {
                if (vec != nullptr && rv_editor_scene_vec_of(e.value, *vec) == RV_OK) {
                    continue;
                }
                o.extra.push_back(e);
                s.read_only = "object '" + o.id + "': '" + e.key + "' is not what this editor writes";
            } else if (e.key == "uv") {
                if (rv_editor_scene_uv_of(e.value, o.uv) != RV_OK) {
                    o.extra.push_back(e);
                    s.read_only = "object '" + o.id + "': 'uv' must be four numbers";
                }
            } else if (e.key == "tint") {
                if (rv_editor_scene_tint_of(e.value, o.tint) != RV_OK) {
                    o.extra.push_back(e);
                    s.read_only = "object '" + o.id + "': 'tint' must be three integers from 0 to 255";
                }
            } else if (e.key == "tess") {
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
        if (!rv_pdklib::rv_version_parse(version_str, major, minor)) {
            error = path.string() + ": malformed [scene] version '" + version_str + "'";
            return false;
        }
        s.version_major = major;
        s.version_minor = minor;
    } else if (has_legacy_format) {
        s.version_major = 0;
        s.version_minor = 0;
    } else {
        error = path.string() + ": no [scene] version";
        return false;
    }
    if (!rv_pdklib::rv_version_compatible(s.version_major, s.version_minor)) {
        s.read_only = "scene version " + std::to_string(s.version_major) + "." + std::to_string(s.version_minor) +
            " is not compatible with this editor's " + rv_pdklib::rv_version_str;
    }
    scene = std::move(s);
    return true;
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
            if (obj.kind == "quad" || obj.kind == "billboard" || obj.kind == "volume") {
                needs_v04 = true;
                break;
            }
            // Check for non-default uv/tint/tess
            if (obj.uv[0] != 0.0 || obj.uv[1] != 0.0 || obj.uv[2] != 0.0 || obj.uv[3] != 0.0) {
                needs_v04 = true;
                break;
            }
            if (obj.tint[0] != 255 || obj.tint[1] != 255 || obj.tint[2] != 255) {
                needs_v04 = true;
                break;
            }
            if (obj.tess != 2.0) {
                needs_v04 = true;
                break;
            }
            // Check if extra has uv/tint/tess keys
            if (rv_editor_scene_extra_has(obj.extra, "uv") || rv_editor_scene_extra_has(obj.extra, "tint") ||
                rv_editor_scene_extra_has(obj.extra, "tess")) {
                needs_v04 = true;
                break;
            }
        }

        // Upgrade to 0.4 if needed, but never downgrade
        if (needs_v04 && version_major == 0 && version_minor < 4) {
            version_major = 0;
            version_minor = 4;
        }
    }

    std::string version = std::to_string(version_major) + "." + std::to_string(version_minor);
    t += "[scene]\nversion = " + rv_editor_toml_quote(version) + "\n";
    rv_editor_scene_entries(t, scene.scene_extra);
    for (const rv_editor_scene_object &o : scene.objects) {
        t += "\n[[object]]\n";
        t += "id = " + rv_editor_toml_quote(o.id) + "\n";
        t += "name = " + rv_editor_toml_quote(o.name) + "\n";
        t += "parent = " + rv_editor_toml_quote(o.parent) + "\n";
        t += "kind = " + rv_editor_toml_quote(o.kind) + "\n";
        t += "position = " + rv_editor_scene_vec(o.position) + "\n";
        t += "rotation = " + rv_editor_scene_vec(o.rotation) + "\n";
        t += "scale = " + rv_editor_scene_vec(o.scale) + "\n";
        t += "mesh = " + rv_editor_toml_quote(o.mesh) + "\n";
        t += "texture = " + rv_editor_toml_quote(o.texture) + "\n";
        if (o.kind == "quad" || o.kind == "billboard") {
            if (!rv_editor_scene_extra_has(o.extra, "uv")) {
                t += "uv = [" + rv_editor_scene_number(o.uv[0]) + ", " + rv_editor_scene_number(o.uv[1]) + ", " +
                    rv_editor_scene_number(o.uv[2]) + ", " + rv_editor_scene_number(o.uv[3]) + "]\n";
            }
            if (!rv_editor_scene_extra_has(o.extra, "tint")) {
                t += "tint = [" + std::to_string(o.tint[0]) + ", " + std::to_string(o.tint[1]) + ", " +
                    std::to_string(o.tint[2]) + "]\n";
            }
            if (!rv_editor_scene_extra_has(o.extra, "tess")) {
                t += "tess = " + rv_editor_scene_number(o.tess) + "\n";
            }
        }
        rv_editor_scene_entries(t, o.extra);
    }
    for (const auto &section : scene.other_sections) {
        t += std::string("\n") + (section.array ? "[[" : "[") + section.name + (section.array ? "]]" : "]") + "\n";
        rv_editor_scene_entries(t, section.entries);
    }
    return t;
}

bool rv_editor_scene_save(const rv_editor_scene &scene, std::string &error)
{
    if (!scene.read_only.empty()) {
        error = "read-only: " + scene.read_only;
        return false;
    }
    return rv_editor_file_replace(scene.path, rv_editor_scene_render(scene), error);
}

rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path)
{
    rv_editor_scene s;
    s.path = path;
    s.preamble = "# A scene: 3dmppc-editor's Scene layout edits it.\n\n";
    rv_editor_scene_object camera;
    camera.id = rv_editor_scene_new_id(s);
    camera.name = "Camera";
    camera.kind = "camera";
    camera.position = { 0.0, 0.8, -4.0 };
    camera.rotation = { 8.0, 0.0, 0.0 };
    s.objects.push_back(camera);
    rv_editor_scene_object box;
    box.id = rv_editor_scene_new_id(s);
    box.name = "Box";
    box.kind = "mesh";
    s.objects.push_back(box);
    return s;
}

std::string rv_editor_scene_new_id(const rv_editor_scene &scene)
{
    static std::mt19937 gen{ std::random_device{}() };
    for (;;) {
        char buf[16];
        const auto r = std::to_chars(buf, buf + sizeof(buf), gen() | 0x10000000u, 16);
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
