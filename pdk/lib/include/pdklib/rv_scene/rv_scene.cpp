// The scene loader: disc.toml's dialect read into objects, and their transforms.

#include "rv_scene.hpp"

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdklib
{

namespace
{

constexpr float rv_scene_degrees = 3.14159265358979f / 180.0f;
constexpr float rv_scene_right_angle_degrees = 90.0f; // right angle for rotation checks
constexpr int rv_scene_rotation_quarters = 4;         // four 90-degree rotations make a full circle
constexpr int rv_scene_vector_components = 3;         // position, rotation, scale vectors
constexpr int rv_scene_uv_components = 4;             // u0, v0, u1, v1 texture rect
constexpr int rv_scene_tint_channels = 3;             // RGB color channels
constexpr double rv_scene_color_max = 255.0;          // max value for each color channel
constexpr int rv_scene_quad_corners_count = 4;        // four corners: TL, TR, BL, BR

// sin/cos of a degree angle, exact 0/1/-1 on a multiple of 90 (any sign, any
// winding) instead of the ~1e-8 float noise std::cos leaves there -- the
// difference an axis-aligned quad's corners (rv_scene_quad_corners) show up.
void rv_scene_sincos(float degrees, float &s, float &c)
{
    const float quarter = std::round(degrees / rv_scene_right_angle_degrees);
    if (quarter * rv_scene_right_angle_degrees == degrees) {
        constexpr float sins[rv_scene_rotation_quarters] = { 0.0f, 1.0f, 0.0f, -1.0f };
        constexpr float coss[rv_scene_rotation_quarters] = { 1.0f, 0.0f, -1.0f, 0.0f };
        int k = static_cast<int>(quarter) % rv_scene_rotation_quarters;
        if (k < 0) {
            k += rv_scene_rotation_quarters;
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

// The lines above the first section's header, as the file has them; the whole text with no section.
std::string rv_scene_preamble(const std::string &text, const rv_manifest_tree &tree)
{
    if (tree.sections.empty()) {
        return text;
    }
    size_t at = 0;
    for (int line = 1; line < tree.sections.front().line && at < text.size(); ++line) {
        const size_t nl = text.find('\n', at);
        at = nl == std::string::npos ? text.size() : nl + 1;
    }
    return text.substr(0, at);
}

// Why a scene number was refused; the texts below follow this order, none first.
enum class rv_scene_fault {
    none,
    not_three_numbers,
    not_four_numbers,
    not_a_number,
    out_of_range,
    not_positive,
    rounds_to_zero,
};

// The message tail of each fault.
constexpr std::array<const char *, 7> rv_scene_fault_texts = { {
    "",
    "must be three numbers",
    "must be four numbers",
    "must be a number",
    "is outside [-524288, 524288) units",
    "must be greater than zero",
    "is below the Q20.12 step 1/4096 and rounds to 0",
} };

std::string rv_scene_fault_text(rv_scene_fault f)
{
    return rv_scene_fault_texts[static_cast<size_t>(f)];
}

// One number of a scene file: decimal units, rounded to the nearest Q20.12 value.
rv_scene_fault rv_scene_number_value(double v, fx &out)
{
    return fx_from_double(v, out) == RV_OK ? rv_scene_fault::none : rv_scene_fault::out_of_range;
}

rv_scene_fault rv_scene_vector(const rv_manifest_tree_entry &e, fx_vec3 &out)
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_vector_components) {
        return rv_scene_fault::not_three_numbers;
    }
    fx v[rv_scene_vector_components] = {};
    for (int i = 0; i < rv_scene_vector_components; ++i) {
        const rv_scene_fault f = rv_scene_number_value(e.value.nums[static_cast<size_t>(i)], v[i]);
        if (f != rv_scene_fault::none) {
            return f;
        }
    }
    out = { v[0], v[1], v[2] };
    return rv_scene_fault::none;
}

rv_scene_fault rv_scene_uv(const rv_manifest_tree_entry &e, fx out[rv_scene_uv_components])
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_uv_components) {
        return rv_scene_fault::not_four_numbers;
    }
    for (int i = 0; i < rv_scene_uv_components; ++i) {
        const rv_scene_fault f = rv_scene_number_value(e.value.nums[static_cast<size_t>(i)], out[i]);
        if (f != rv_scene_fault::none) {
            return f;
        }
    }
    return rv_scene_fault::none;
}

int rv_scene_tint(const rv_manifest_tree_entry &e, rv_color &out)
{
    if (e.value.kind != rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_tint_channels) {
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < rv_scene_tint_channels; ++i) {
        const double v = e.value.nums[static_cast<size_t>(i)];
        if (v < 0.0 || v > rv_scene_color_max || v != static_cast<double>(static_cast<int>(v))) {
            return RV_ERR_INVAL;
        }
    }
    out = { static_cast<uint8_t>(e.value.nums[0]),
        static_cast<uint8_t>(e.value.nums[1]),
        static_cast<uint8_t>(e.value.nums[2]) };
    return RV_OK;
}

rv_scene_fault rv_scene_tess(const rv_manifest_tree_entry &e, fx &out)
{
    fx q = 0;
    double v = 0.0;
    rv_scene_fault f = rv_scene_fault::not_a_number;
    if (e.value.kind == rv_manifest_value_kind::integer) {
        v = static_cast<double>(e.value.num);
        f = fx_from_units(e.value.num, q) == RV_OK ? rv_scene_fault::none : rv_scene_fault::out_of_range;
    } else if (e.value.kind == rv_manifest_value_kind::real) {
        v = e.value.real;
        f = rv_scene_number_value(v, q);
    }
    if (f != rv_scene_fault::none) {
        return f;
    }
    if (v <= 0.0) {
        return rv_scene_fault::not_positive;
    }
    if (q == 0) {
        return rv_scene_fault::rounds_to_zero;
    }
    out = q;
    return rv_scene_fault::none;
}

// The [scene] version, or the pre-version `format` header, into `out`; a version that is
// missing, malformed or not compatible is reported in `problems`.
void rv_scene_read_version(const rv_manifest_tree &tree, const std::string &origin, rv_scene &out, std::string &problems)
{
    bool has_version = false;
    bool has_legacy_format = false;
    std::string version_str;
    for (const rv_manifest_tree_section &section : tree.sections) {
        if (section.name != "scene" || section.array) {
            continue;
        }
        for (const rv_manifest_tree_entry &e : section.entries) {
            if (e.key == "version" && e.value.kind == rv_manifest_value_kind::string) {
                version_str = e.value.str;
                has_version = true;
            } else if (e.key == "format" && e.value.kind == rv_manifest_value_kind::integer) {
                has_legacy_format = true;
            }
        }
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
            problems += origin + ": scene version 0.0 is not compatible with this PDK's " + std::string(rv_version_str) + "\n";
        }
    } else {
        problems += origin + ": no [scene] version\n";
    }
}

// A [[component]] as read: the id of the object it names, resolved to an index after all objects.
struct rv_scene_component_ref {
    std::string object;
    int line = 0;
    rv_scene_component component;
};

// `object` and `name` are required strings; every other key is a field of the component.
int rv_scene_component_read(const std::string &origin,
    const rv_manifest_tree_section &section,
    rv_scene_component_ref &out,
    std::string &problems)
{
    out.line = section.line;
    bool ok = true;
    for (const rv_manifest_tree_entry &e : section.entries) {
        const bool text_value = e.value.kind == rv_manifest_value_kind::string;
        std::string *field = e.key == "object" ? &out.object : e.key == "name" ? &out.component.name : nullptr;
        if (field == nullptr) {
            out.component.fields.push_back(e);
        } else if (!text_value) {
            problems += rv_scene_at(origin, e.line) + "'" + e.key + "' must be a string\n";
            ok = false;
        } else {
            *field = e.value.str;
        }
    }
    if (out.object.empty()) {
        problems += rv_scene_at(origin, section.line) + "component without an object\n";
        ok = false;
    }
    if (out.component.name.empty()) {
        problems += rv_scene_at(origin, section.line) + "component without a name\n";
        ok = false;
    }
    return ok ? RV_OK : RV_ERR_INVAL;
}

} // namespace

int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error)
{
    rv_manifest_tree tree;
    if (rv_manifest_read_tree(text, origin, tree, error) != 0) {
        return 1;
    }
    rv_scene out;
    out.preamble = rv_scene_preamble(text, tree);
    std::string problems;
    std::vector<std::pair<std::string, int>> parents; // each object's parent id and line
    std::map<std::string, int32_t> index;
    std::vector<rv_scene_component_ref> components;
    for (const rv_manifest_tree_section &section : tree.sections) {
        if (section.name == "scene" && !section.array) {
            for (const rv_manifest_tree_entry &e : section.entries) {
                const bool version_key = (e.key == "version" && e.value.kind == rv_manifest_value_kind::string) ||
                    (e.key == "format" && e.value.kind == rv_manifest_value_kind::integer);
                if (!version_key) {
                    out.scene_extra.push_back(e);
                }
            }
            continue;
        }
        if (section.name == "component" && section.array) {
            rv_scene_component_ref c;
            if (rv_scene_component_read(origin, section, c, problems) == RV_OK) {
                components.push_back(std::move(c));
            }
            continue;
        }
        if (section.name != "object" || !section.array) {
            out.other_sections.push_back(section);
            continue;
        }
        rv_scene_object o;
        std::string parent;
        for (const rv_manifest_tree_entry &e : section.entries) {
            const bool text_value = e.value.kind == rv_manifest_value_kind::string;
            std::string *field = e.key == "id" ? &o.id :
                e.key == "name"                ? &o.name :
                e.key == "kind"                ? &o.kind :
                e.key == "entity"              ? &o.entity :
                e.key == "mesh"                ? &o.mesh :
                e.key == "texture"             ? &o.texture :
                e.key == "parent"              ? &parent :
                                                 nullptr;
            fx_vec3 *vec = e.key == "position" ? &o.position :
                e.key == "rotation"            ? &o.rotation :
                e.key == "scale"               ? &o.scale :
                                                 nullptr;
            if (field != nullptr && text_value) {
                *field = e.value.str;
            } else if (field != nullptr) {
                problems += rv_scene_at(origin, e.line) + "'" + e.key + "' must be a string\n";
            } else if (vec != nullptr) {
                const rv_scene_fault f = rv_scene_vector(e, *vec);
                if (f != rv_scene_fault::none) {
                    problems += rv_scene_at(origin, e.line) + "'" + e.key + "' " + rv_scene_fault_text(f) + "\n";
                }
            } else if (e.key == "uv") {
                const rv_scene_fault f = rv_scene_uv(e, o.uv);
                if (f != rv_scene_fault::none) {
                    problems += rv_scene_at(origin, e.line) + "'uv' " + rv_scene_fault_text(f) + "\n";
                }
            } else if (e.key == "tint") {
                if (rv_scene_tint(e, o.tint) != RV_OK) {
                    problems += rv_scene_at(origin, e.line) + "'tint' must be three integers from 0 to 255\n";
                }
            } else if (e.key == "tess") {
                const rv_scene_fault f = rv_scene_tess(e, o.tess);
                if (f != rv_scene_fault::none) {
                    problems += rv_scene_at(origin, e.line) + "'tess' " + rv_scene_fault_text(f) + "\n";
                }
            } else {
                o.extra.push_back(e);
            }
        }
        if (o.id.empty()) {
            problems += rv_scene_at(origin, section.line) + "object without an id\n";
            continue;
        }
        if (o.kind != "group" && o.kind != "camera" && o.kind != "mesh" && o.kind != "quad" && o.kind != "billboard" &&
            o.kind != "volume") {
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
    rv_scene_read_version(tree, origin, out, problems);
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
            problems += rv_scene_at(origin, parents[i].second) + "object '" + out.objects[i].id + "' is its own ancestor\n";
            cyclic.push_back(i);
        }
    }
    for (const size_t i : cyclic) {
        out.objects[i].parent = -1;
    }
    // A component names its object by id, so it resolves only once every object is read.
    std::set<std::pair<int32_t, std::string>> named;
    for (rv_scene_component_ref &c : components) {
        const auto it = index.find(c.object);
        if (it == index.end()) {
            problems += rv_scene_at(origin, c.line) + "component '" + c.component.name + "' names an unknown object '" +
                c.object + "'\n";
            continue;
        }
        c.component.object = it->second;
        if (!named.emplace(it->second, c.component.name).second) {
            problems +=
                rv_scene_at(origin, c.line) + "object '" + c.object + "' has component '" + c.component.name + "' twice\n";
            continue;
        }
        out.components.push_back(std::move(c.component));
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
    const rv_vec3 position = fx_vec3_to_float(o.position);
    const rv_vec3 rotation = fx_vec3_to_float(o.rotation);
    const rv_vec3 scale = fx_vec3_to_float(o.scale);
    const rv_mat4 r =
        rv_mat4_mul(rv_scene_rotate_y(rotation.y), rv_mat4_mul(rv_scene_rotate_x(rotation.x), rv_scene_rotate_z(rotation.z)));
    return rv_mat4_mul(rv_mat4_translate(position), rv_mat4_mul(r, rv_mat4_scale(scale)));
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

void rv_scene_quad_corners(const rv_scene &scene, std::size_t index, rv_vec3 out[rv_scene_quad_corners_count])
{
    const rv_mat4 world = rv_scene_world(scene, index);
    // Local unit square, +y up, front face towards +z: top-left, top-right,
    // bottom-left, bottom-right -- the PDK's quad Z order.
    const rv_vec3 local[rv_scene_quad_corners_count] = {
        { -0.5f, 0.5f, 0.0f },
        { 0.5f, 0.5f, 0.0f },
        { -0.5f, -0.5f, 0.0f },
        { 0.5f, -0.5f, 0.0f },
    };
    for (int i = 0; i < rv_scene_quad_corners_count; ++i) {
        out[i] = rv_mat4_mul_point(world, local[i]);
    }
}

} // namespace rv_pdklib
