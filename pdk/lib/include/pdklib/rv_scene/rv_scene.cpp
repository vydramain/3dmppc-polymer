// The scene loader: disc.toml's dialect read into objects, and their transforms.

#include "rv_scene.hpp"

#include <map>
#include <utility>

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdklib
{

namespace
{

constexpr float rv_scene_degrees = 3.14159265358979f / 180.0f;

std::string rv_scene_at(const std::string &origin, int line)
{
    return origin + ":" + std::to_string(line) + ": ";
}

bool rv_scene_vector(const rv_manifest_tree_entry &e, rv_vec3 &out)
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != 3) {
        return false;
    }
    out = { static_cast<float>(e.value.nums[0]), static_cast<float>(e.value.nums[1]),
        static_cast<float>(e.value.nums[2]) };
    return true;
}

} // namespace

int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error)
{
    rv_manifest_tree tree;
    if (rv_manifest_read_tree(text, origin, tree, error) != 0) {
        return 1;
    }
    rv_scene out;
    std::string problems;
    std::vector<std::pair<std::string, int>> parents; // each object's parent id and line
    std::map<std::string, int32_t> index;
    bool has_version = false;
    bool has_legacy_format = false;
    std::string version_str;
    for (const rv_manifest_tree_section &section : tree.sections) {
        if (section.name == "scene" && !section.array) {
            for (const rv_manifest_tree_entry &e : section.entries) {
                if (e.key == "version" && e.value.kind == rv_manifest_value_kind::string) {
                    version_str = e.value.str;
                    has_version = true;
                } else if (e.key == "format" && e.value.kind == rv_manifest_value_kind::integer) {
                    has_legacy_format = true;
                }
            }
            continue;
        }
        if (section.name != "object" || !section.array) {
            continue;
        }
        rv_scene_object o;
        std::string parent;
        for (const rv_manifest_tree_entry &e : section.entries) {
            const bool text_value = e.value.kind == rv_manifest_value_kind::string;
            std::string *field = e.key == "id" ? &o.id : e.key == "name" ? &o.name : e.key == "kind" ? &o.kind
                : e.key == "mesh"                                     ? &o.mesh
                : e.key == "texture"                                  ? &o.texture
                : e.key == "parent"                                   ? &parent
                                                                      : nullptr;
            rv_vec3 *vec = e.key == "position" ? &o.position : e.key == "rotation" ? &o.rotation
                : e.key == "scale"                                                 ? &o.scale
                                                                                   : nullptr;
            if (field != nullptr && text_value) {
                *field = e.value.str;
            } else if (field != nullptr) {
                problems += rv_scene_at(origin, e.line) + "'" + e.key + "' must be a string\n";
            } else if (vec != nullptr && !rv_scene_vector(e, *vec)) {
                problems += rv_scene_at(origin, e.line) + "'" + e.key + "' must be three numbers\n";
            }
        }
        if (o.id.empty()) {
            problems += rv_scene_at(origin, section.line) + "object without an id\n";
            continue;
        }
        if (o.kind != "group" && o.kind != "camera" && o.kind != "mesh") {
            problems += rv_scene_at(origin, section.line) + "object '" + o.id + "' has kind '" + o.kind +
                "': group, camera or mesh\n";
        }
        if (!index.emplace(o.id, static_cast<int32_t>(out.objects.size())).second) {
            problems += rv_scene_at(origin, section.line) + "id '" + o.id + "' is used twice\n";
            continue;
        }
        parents.emplace_back(parent, section.line);
        out.objects.push_back(std::move(o));
    }
    if (has_version) {
        uint32_t major = 0, minor = 0;
        if (!rv_version_parse(version_str, major, minor)) {
            problems += origin + ": malformed [scene] version '" + version_str + "'\n";
        } else if (!rv_version_compatible(major, minor)) {
            problems += origin + ": scene version " + std::to_string(major) + "." + std::to_string(minor) +
                " is not compatible with this PDK's " + rv_version_str + "\n";
        } else {
            out.version_major = major;
            out.version_minor = minor;
        }
    } else if (has_legacy_format) {
        // The pre-version header: read as 0.0, compatible only while the PDK major is 0.
        if (!rv_version_compatible(0, 0)) {
            problems += origin + ": scene version 0.0 is not compatible with this PDK's " +
                std::string(rv_version_str) + "\n";
        }
    } else {
        problems += origin + ": no [scene] version\n";
    }
    for (size_t i = 0; i < out.objects.size(); ++i) {
        const std::string &parent = parents[i].first;
        if (parent.empty()) {
            continue;
        }
        const auto it = index.find(parent);
        if (it == index.end()) {
            problems += rv_scene_at(origin, parents[i].second) + "object '" + out.objects[i].id +
                "' names an unknown parent '" + parent + "'\n";
            continue;
        }
        out.objects[i].parent = it->second;
    }
    // A chain longer than the scene has objects has come round again. Every member is
    // found first, on the parents as read, and only then cut loose.
    std::vector<size_t> cyclic;
    for (size_t i = 0; i < out.objects.size(); ++i) {
        int32_t p = out.objects[i].parent;
        size_t steps = 0;
        while (p >= 0 && steps <= out.objects.size()) {
            p = out.objects[static_cast<size_t>(p)].parent;
            ++steps;
        }
        if (p >= 0) {
            problems += rv_scene_at(origin, parents[i].second) + "object '" + out.objects[i].id +
                "' is its own ancestor\n";
            cyclic.push_back(i);
        }
    }
    for (const size_t i : cyclic) {
        out.objects[i].parent = -1;
    }
    if (!problems.empty()) {
        error = problems;
        return 1;
    }
    scene = std::move(out);
    return 0;
}

rv_mat4 rv_scene_local(const rv_scene_object &o)
{
    const rv_mat4 r = rv_mat4_mul(rv_mat4_rotate_y(o.rotation.y * rv_scene_degrees),
        rv_mat4_mul(rv_mat4_rotate_x(o.rotation.x * rv_scene_degrees), rv_mat4_rotate_z(o.rotation.z * rv_scene_degrees)));
    return rv_mat4_mul(rv_mat4_translate(o.position), rv_mat4_mul(r, rv_mat4_scale(o.scale)));
}

rv_mat4 rv_scene_world(const rv_scene &scene, std::size_t index)
{
    rv_mat4 m = rv_scene_local(scene.objects[index]);
    size_t steps = 0;
    for (int32_t p = scene.objects[index].parent; p >= 0 && steps < scene.objects.size(); ++steps) {
        m = rv_mat4_mul(rv_scene_local(scene.objects[static_cast<size_t>(p)]), m);
        p = scene.objects[static_cast<size_t>(p)].parent;
    }
    return m;
}

} // namespace rv_pdklib
