// The scene loader: disc.toml's dialect read into objects, and their transforms.

#include "rv_scene.hpp"

#include <cmath>
#include <map>
#include <utility>

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdklib
{

namespace
{

constexpr float rv_scene_degrees = 3.14159265358979f / 180.0f;

// sin/cos of a degree angle, exact 0/1/-1 on a multiple of 90 (any sign, any
// winding) instead of the ~1e-8 float noise std::cos leaves there -- the
// difference an axis-aligned quad's corners (rv_scene_quad_corners) show up.
void rv_scene_sincos(float degrees, float &s, float &c)
{
    const float quarter = std::round(degrees / 90.0f);
    if (quarter * 90.0f == degrees) {
        constexpr float sins[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
        constexpr float coss[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
        int k = static_cast<int>(quarter) % 4;
        if (k < 0) {
            k += 4;
        }
        s = sins[k];
        c = coss[k];
        return;
    }
    const float radians = degrees * rv_scene_degrees;
    s = std::sin(radians);
    c = std::cos(radians);
}

// Same layout as rv_mat4_rotate_x/y/z (rv_math.hpp), built from rv_scene_sincos.
rv_mat4 rv_scene_rotate_x(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
}

rv_mat4 rv_scene_rotate_y(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
}

rv_mat4 rv_scene_rotate_z(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    return r;
}

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

bool rv_scene_uv(const rv_manifest_tree_entry &e, float out[4])
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != 4) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<float>(e.value.nums[i]);
    }
    return true;
}

bool rv_scene_tint(const rv_manifest_tree_entry &e, rv_color &out)
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != 3) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const double v = e.value.nums[static_cast<size_t>(i)];
        if (v < 0.0 || v > 255.0 || v != static_cast<double>(static_cast<int>(v))) {
            return false;
        }
    }
    out = { static_cast<uint8_t>(e.value.nums[0]), static_cast<uint8_t>(e.value.nums[1]),
        static_cast<uint8_t>(e.value.nums[2]) };
    return true;
}

bool rv_scene_tess(const rv_manifest_tree_entry &e, float &out)
{
    double v;
    if (e.value.kind == rv_manifest_value_kind::integer) {
        v = static_cast<double>(e.value.num);
    } else if (e.value.kind == rv_manifest_value_kind::real) {
        v = e.value.real;
    } else {
        return false;
    }
    if (v <= 0.0) {
        return false;
    }
    out = static_cast<float>(v);
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
            } else if (e.key == "uv" && !rv_scene_uv(e, o.uv)) {
                problems += rv_scene_at(origin, e.line) + "'uv' must be four numbers\n";
            } else if (e.key == "tint" && !rv_scene_tint(e, o.tint)) {
                problems += rv_scene_at(origin, e.line) + "'tint' must be three integers from 0 to 255\n";
            } else if (e.key == "tess" && !rv_scene_tess(e, o.tess)) {
                problems += rv_scene_at(origin, e.line) + "'tess' must be a number greater than zero\n";
            }
        }
        if (o.id.empty()) {
            problems += rv_scene_at(origin, section.line) + "object without an id\n";
            continue;
        }
        if (o.kind != "group" && o.kind != "camera" && o.kind != "mesh" && o.kind != "quad" &&
            o.kind != "billboard" && o.kind != "volume") {
            problems += rv_scene_at(origin, section.line) + "object '" + o.id + "' has kind '" + o.kind +
                "': group, camera, mesh, quad, billboard or volume\n";
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
        if (rv_version_parse(version_str, major, minor) != RV_OK) {
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
    const rv_mat4 r = rv_mat4_mul(
        rv_scene_rotate_y(o.rotation.y), rv_mat4_mul(rv_scene_rotate_x(o.rotation.x), rv_scene_rotate_z(o.rotation.z)));
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

void rv_scene_quad_corners(const rv_scene &scene, std::size_t index, rv_vec3 out[4])
{
    const rv_mat4 world = rv_scene_world(scene, index);
    // Local unit square, +y up, front face towards +z: top-left, top-right,
    // bottom-left, bottom-right -- the PDK's quad Z order.
    const rv_vec3 local[4] = {
        { -0.5f, 0.5f, 0.0f },
        { 0.5f, 0.5f, 0.0f },
        { -0.5f, -0.5f, 0.0f },
        { 0.5f, -0.5f, 0.0f },
    };
    for (int i = 0; i < 4; ++i) {
        out[i] = rv_mat4_mul_point(world, local[i]);
    }
}

} // namespace rv_pdklib
