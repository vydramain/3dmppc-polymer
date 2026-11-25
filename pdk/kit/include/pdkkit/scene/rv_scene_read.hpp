#pragma once

// The scene loader, inline: a disc gets the SDK as headers only, with no object code.
// Included by rv_scene.hpp first, before rv_scene_write.hpp.

#include "pdkkit/scene/rv_scene.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"

namespace rv_pdkkit
{

namespace detail
{

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

// Keys of form 1, removed: an object's data was given as these fields. Form 2 gives it as [[component]] blocks.
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
            out.scene_extra.insert(out.scene_extra.end(), section.entries.begin(), section.entries.end());
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
        std::string form1; // the form-1 keys of this object, for its one problem
        for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
            const bool text_value = e.value.kind == rv_pdklib::rv_manifest_value_kind::string;
            std::string *field = e.key == "id" ? &o.id :
                e.key == "entity"              ? &o.entity :
                e.key == "parent"              ? &parent :
                                                 nullptr;
            if (field != nullptr && text_value) {
                *field = e.value.str;
            } else if (field != nullptr) {
                problems += detail::rv_scene_at(origin, e.line) + "'" + e.key + "' must be a string\n";
            } else if (std::find(detail::rv_scene_form1_keys.begin(), detail::rv_scene_form1_keys.end(), e.key) !=
                detail::rv_scene_form1_keys.end()) {
                form1 += (form1.empty() ? "" : ", ") + e.key;
            } else {
                o.extra.push_back(e);
            }
        }
        if (!form1.empty()) {
            problems += detail::rv_scene_at(origin, section.line) + "object is in form 1 (" + form1 +
                "), which is removed: give its data as [[component]] blocks with object = its id (name, transform, "
                "model, camera, volume, legacy_quad, legacy_billboard)\n";
        }
        if (o.id.empty()) {
            problems += detail::rv_scene_at(origin, section.line) + "object without an id\n";
            continue;
        }
        if (!index.emplace(o.id, static_cast<int32_t>(out.objects.size())).second) {
            problems += detail::rv_scene_at(origin, section.line) + "id '" + o.id + "' is used twice\n";
            continue;
        }
        parents.emplace_back(parent, section.line);
        out.objects.push_back(std::move(o));
    }
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

} // namespace rv_pdkkit
