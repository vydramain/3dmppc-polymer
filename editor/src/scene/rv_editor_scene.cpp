// Scene documents: read with pdklib's dialect, written back whole with the keys
// the editor does not know, never rewritten when their format is newer.

#include "scene/rv_editor_scene.hpp"

#include <charconv>
#include <random>

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

bool rv_editor_scene_vec_of(const rv_pdklib::rv_manifest_mvalue &v, rv_editor_vec3 &out)
{
    if (v.kind != kind::numbers || v.nums.size() != 3) {
        return false;
    }
    out = { v.nums[0], v.nums[1], v.nums[2] };
    return true;
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
    s.format = 0;
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
                if (e.key == "format" && e.value.kind == kind::integer) {
                    s.format = e.value.num;
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
            } else if (vec == nullptr || !rv_editor_scene_vec_of(e.value, *vec)) {
                o.extra.push_back(e);
                if (field != nullptr || vec != nullptr) {
                    s.read_only = "object '" + o.id + "': '" + e.key + "' is not what this editor writes";
                }
            }
        }
        s.objects.push_back(std::move(o));
    }
    if (s.format == 0) {
        error = path.string() + ": no [scene] format";
        return false;
    }
    if (s.format > rv_editor_scene_format) {
        s.read_only = "format " + std::to_string(s.format) + " is newer than this editor's " +
            std::to_string(rv_editor_scene_format);
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
    t += "[scene]\nformat = " + std::to_string(scene.format) + "\n";
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
    s.preamble = "# A scene (editor/docs/adr/0009-scene-document.md): 3dmppc-editor's Scene layout edits it.\n\n";
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
