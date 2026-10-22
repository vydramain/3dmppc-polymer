#pragma once

// The scene loader and its transforms, inline: a disc gets the SDK as headers only, with no object code.
// Included by rv_scene.hpp first, before rv_scene_write.hpp.

#include "pdkkit/scene/rv_scene.hpp"
#include "pdklib/rv_scene/rv_scene_space.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdkkit
{

namespace detail
{

inline constexpr int rv_scene_vector_components = 3;  // position, rotation, scale vectors
inline constexpr int rv_scene_uv_components = 4;      // u0, v0, u1, v1 texture rect
inline constexpr int rv_scene_tint_channels = 3;      // RGB color channels
inline constexpr double rv_scene_color_max = 255.0;   // max value for each color channel
inline constexpr int rv_scene_quad_corners_count = 4; // four corners: TL, TR, BL, BR

inline std::string rv_scene_at(const std::string &origin, int line)
{
    return origin + ":" + std::to_string(line) + ": ";
}

// The lines above the first section's header, as the file has them; the whole text with no section.
inline std::string rv_scene_preamble(const std::string &text, const rv_pdklib::rv_manifest_tree &tree)
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
inline constexpr std::array<const char *, 7> rv_scene_fault_texts = { {
    "",
    "must be three numbers",
    "must be four numbers",
    "must be a number",
    "is outside [-524288, 524288) units",
    "must be greater than zero",
    "is below the Q20.12 step 1/4096 and rounds to 0",
} };

inline std::string rv_scene_fault_text(rv_scene_fault f)
{
    return rv_scene_fault_texts[static_cast<size_t>(f)];
}

// One number of a scene file: decimal units, rounded to the nearest Q20.12 value.
inline rv_scene_fault rv_scene_number_value(double v, rv_pdklib::fx &out)
{
    return rv_pdklib::fx_from_double(v, out) == RV_OK ? rv_scene_fault::none : rv_scene_fault::out_of_range;
}

inline rv_scene_fault rv_scene_vector(const rv_pdklib::rv_manifest_tree_entry &e, rv_pdklib::fx_vec3 &out)
{
    if (e.value.kind != rv_pdklib::rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_vector_components) {
        return rv_scene_fault::not_three_numbers;
    }
    rv_pdklib::fx v[rv_scene_vector_components] = {};
    for (int i = 0; i < rv_scene_vector_components; ++i) {
        const rv_scene_fault f = rv_scene_number_value(e.value.nums[static_cast<size_t>(i)], v[i]);
        if (f != rv_scene_fault::none) {
            return f;
        }
    }
    out = { v[0], v[1], v[2] };
    return rv_scene_fault::none;
}

inline rv_scene_fault rv_scene_uv(const rv_pdklib::rv_manifest_tree_entry &e, rv_pdklib::fx out[rv_scene_uv_components])
{
    if (e.value.kind != rv_pdklib::rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_uv_components) {
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

inline int rv_scene_tint(const rv_pdklib::rv_manifest_tree_entry &e, rv_color &out)
{
    if (e.value.kind != rv_pdklib::rv_manifest_value_kind::numbers || e.value.nums.size() != rv_scene_tint_channels) {
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

inline rv_scene_fault rv_scene_tess(const rv_pdklib::rv_manifest_tree_entry &e, rv_pdklib::fx &out)
{
    rv_pdklib::fx q = 0;
    double v = 0.0;
    rv_scene_fault f = rv_scene_fault::not_a_number;
    if (e.value.kind == rv_pdklib::rv_manifest_value_kind::integer) {
        v = static_cast<double>(e.value.num);
        f = rv_pdklib::fx_from_units(e.value.num, q) == RV_OK ? rv_scene_fault::none : rv_scene_fault::out_of_range;
    } else if (e.value.kind == rv_pdklib::rv_manifest_value_kind::real) {
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
inline void
rv_scene_read_version(const rv_pdklib::rv_manifest_tree &tree, const std::string &origin, rv_scene &out, std::string &problems)
{
    bool has_version = false;
    bool has_legacy_format = false;
    std::string version_str;
    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        if (section.name != "scene" || section.array) {
            continue;
        }
        for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
            if (e.key == "version" && e.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                version_str = e.value.str;
                has_version = true;
            } else if (e.key == "format" && e.value.kind == rv_pdklib::rv_manifest_value_kind::integer) {
                has_legacy_format = true;
            }
        }
    }
    if (has_version) {
        uint32_t major = 0, minor = 0;
        if (rv_pdklib::rv_version_parse(version_str, major, minor) != RV_OK) {
            problems += origin + ": malformed [scene] version '" + version_str + "'\n";
        } else if (!rv_pdklib::rv_version_compatible(major, minor)) {
            problems += origin + ": scene version " + std::to_string(major) + "." + std::to_string(minor) +
                " is not compatible with this PDK's " + rv_pdklib::rv_version_str + "\n";
        } else {
            out.version_major = major;
            out.version_minor = minor;
        }
    } else if (has_legacy_format) {
        // The pre-version header: read as 0.0, compatible only while the PDK major is 0.
        if (!rv_pdklib::rv_version_compatible(0, 0)) {
            problems += origin + ": scene version 0.0 is not compatible with this PDK's " +
                std::string(rv_pdklib::rv_version_str) + "\n";
        }
    } else {
        problems += origin + ": no [scene] version\n";
    }
}

// An [[object]] with any of these keys is form 1 and needs a kind; one with none is form 2.
inline constexpr std::array<const char *, 10> rv_scene_form1_keys = { {
    "name",
    "kind",
    "position",
    "rotation",
    "scale",
    "mesh",
    "texture",
    "uv",
    "tint",
    "tess",
} };

inline bool rv_scene_is_form1(const rv_pdklib::rv_manifest_tree_section &section)
{
    return std::any_of(section.entries.begin(), section.entries.end(), [](const rv_pdklib::rv_manifest_tree_entry &e) {
        return std::find(rv_scene_form1_keys.begin(), rv_scene_form1_keys.end(), e.key) != rv_scene_form1_keys.end();
    });
}

// A [[component]] as read: the id of the object it names, resolved to an index after all objects.
struct rv_scene_component_ref {
    std::string object;
    int line = 0;
    rv_scene_component component;
};

// `object` and `name` are required strings; every other key is a field of the component.
inline int rv_scene_component_read(const std::string &origin,
    const rv_pdklib::rv_manifest_tree_section &section,
    rv_scene_component_ref &out,
    std::string &problems)
{
    out.line = section.line;
    bool ok = true;
    for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
        const bool text_value = e.value.kind == rv_pdklib::rv_manifest_value_kind::string;
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

} // namespace detail

inline int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error)
{
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text, origin, tree, error) != 0) {
        return 1;
    }
    rv_scene out;
    out.preamble = detail::rv_scene_preamble(text, tree);
    std::string problems;
    std::vector<std::pair<std::string, int>> parents; // each object's parent id and line
    std::map<std::string, int32_t> index;
    std::vector<detail::rv_scene_component_ref> components;
    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        if (section.name == "scene" && !section.array) {
            for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
                const bool version_key = (e.key == "version" && e.value.kind == rv_pdklib::rv_manifest_value_kind::string) ||
                    (e.key == "format" && e.value.kind == rv_pdklib::rv_manifest_value_kind::integer);
                if (!version_key) {
                    out.scene_extra.push_back(e);
                }
            }
            continue;
        }
        if (section.name == "component" && section.array) {
            detail::rv_scene_component_ref c;
            if (detail::rv_scene_component_read(origin, section, c, problems) == RV_OK) {
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
        for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
            const bool text_value = e.value.kind == rv_pdklib::rv_manifest_value_kind::string;
            std::string *field = e.key == "id" ? &o.id :
                e.key == "name"                ? &o.name :
                e.key == "kind"                ? &o.kind :
                e.key == "entity"              ? &o.entity :
                e.key == "mesh"                ? &o.mesh :
                e.key == "texture"             ? &o.texture :
                e.key == "parent"              ? &parent :
                                                 nullptr;
            rv_pdklib::fx_vec3 *vec = e.key == "position" ? &o.position :
                e.key == "rotation"                       ? &o.rotation :
                e.key == "scale"                          ? &o.scale :
                                                            nullptr;
            if (field != nullptr && text_value) {
                *field = e.value.str;
            } else if (field != nullptr) {
                problems += detail::rv_scene_at(origin, e.line) + "'" + e.key + "' must be a string\n";
            } else if (vec != nullptr) {
                const detail::rv_scene_fault f = detail::rv_scene_vector(e, *vec);
                if (f != detail::rv_scene_fault::none) {
                    problems +=
                        detail::rv_scene_at(origin, e.line) + "'" + e.key + "' " + detail::rv_scene_fault_text(f) + "\n";
                }
            } else if (e.key == "uv") {
                const detail::rv_scene_fault f = detail::rv_scene_uv(e, o.uv);
                if (f != detail::rv_scene_fault::none) {
                    problems += detail::rv_scene_at(origin, e.line) + "'uv' " + detail::rv_scene_fault_text(f) + "\n";
                }
            } else if (e.key == "tint") {
                if (detail::rv_scene_tint(e, o.tint) != RV_OK) {
                    problems += detail::rv_scene_at(origin, e.line) + "'tint' must be three integers from 0 to 255\n";
                }
            } else if (e.key == "tess") {
                const detail::rv_scene_fault f = detail::rv_scene_tess(e, o.tess);
                if (f != detail::rv_scene_fault::none) {
                    problems += detail::rv_scene_at(origin, e.line) + "'tess' " + detail::rv_scene_fault_text(f) + "\n";
                }
            } else {
                o.extra.push_back(e);
            }
        }
        if (o.id.empty()) {
            problems += detail::rv_scene_at(origin, section.line) + "object without an id\n";
            continue;
        }
        // A form-2 object has no kind to check.
        if (detail::rv_scene_is_form1(section) && o.kind != "group" && o.kind != "camera" && o.kind != "mesh" &&
            o.kind != "quad" && o.kind != "billboard" && o.kind != "volume") {
            problems += detail::rv_scene_at(origin, section.line) + "object '" + o.id + "' has kind '" + o.kind +
                "': group, camera, mesh, quad, billboard or volume\n";
        }
        if (!index.emplace(o.id, static_cast<int32_t>(out.objects.size())).second) {
            problems += detail::rv_scene_at(origin, section.line) + "id '" + o.id + "' is used twice\n";
            continue;
        }
        parents.emplace_back(parent, section.line);
        out.objects.push_back(std::move(o));
    }
    detail::rv_scene_read_version(tree, origin, out, problems);
    for (size_t i = 0; i < out.objects.size(); ++i) {
        const std::string &parent = parents[i].first;
        if (parent.empty()) {
            continue;
        }
        const auto it = index.find(parent);
        if (it == index.end()) {
            problems += detail::rv_scene_at(origin, parents[i].second) + "object '" + out.objects[i].id +
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
            problems +=
                detail::rv_scene_at(origin, parents[i].second) + "object '" + out.objects[i].id + "' is its own ancestor\n";
            cyclic.push_back(i);
        }
    }
    for (const size_t i : cyclic) {
        out.objects[i].parent = -1;
    }
    // A component names its object by id, so it resolves only once every object is read.
    std::set<std::pair<int32_t, std::string>> named;
    for (detail::rv_scene_component_ref &c : components) {
        const auto it = index.find(c.object);
        if (it == index.end()) {
            problems += detail::rv_scene_at(origin, c.line) + "component '" + c.component.name + "' names an unknown object '" +
                c.object + "'\n";
            continue;
        }
        c.component.object = it->second;
        if (!named.emplace(it->second, c.component.name).second) {
            problems += detail::rv_scene_at(origin, c.line) + "object '" + c.object + "' has component '" + c.component.name +
                "' twice\n";
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

inline rv_pdklib::rv_mat4 rv_scene_local(const rv_scene_object &o)
{
    return rv_pdklib::rv_scene_local_trs(o.position, o.rotation, o.scale);
}

inline rv_pdklib::rv_mat4 rv_scene_world(const rv_scene &scene, std::size_t index)
{
    rv_pdklib::rv_mat4 m = rv_scene_local(scene.objects[index]);
    size_t steps = 0;
    for (int32_t p = scene.objects[index].parent; p >= 0 && steps < scene.objects.size(); ++steps) {
        m = rv_pdklib::rv_mat4_mul(rv_scene_local(scene.objects[static_cast<size_t>(p)]), m);
        p = scene.objects[static_cast<size_t>(p)].parent;
    }
    return m;
}

inline void
rv_scene_quad_corners(const rv_scene &scene, std::size_t index, rv_pdklib::rv_vec3 out[detail::rv_scene_quad_corners_count])
{
    const rv_pdklib::rv_mat4 world = rv_scene_world(scene, index);
    // Local unit square, +y up, front face towards +z: top-left, top-right,
    // bottom-left, bottom-right -- the PDK's quad Z order.
    const rv_pdklib::rv_vec3 local[detail::rv_scene_quad_corners_count] = {
        { -0.5f, 0.5f, 0.0f },
        { 0.5f, 0.5f, 0.0f },
        { -0.5f, -0.5f, 0.0f },
        { 0.5f, -0.5f, 0.0f },
    };
    for (int i = 0; i < detail::rv_scene_quad_corners_count; ++i) {
        out[i] = rv_pdklib::rv_mat4_mul_point(world, local[i]);
    }
}

} // namespace rv_pdkkit
