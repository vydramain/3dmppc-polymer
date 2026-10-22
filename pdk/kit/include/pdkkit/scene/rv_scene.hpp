#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cv/rv_vertex.h"
#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace rv_pdkkit
{

// A scene document as a disc reads it:
// objects in file order with their parents resolved. It takes the file's BYTES,
// never a path: reading is the drive's (rv_cd_asset_*). A disc that draws no
// scene never calls it.
//
// [scene] carries version = "M.m", the PDK version the file was written
// against; a file is read when rv_version_compatible(major, minor) holds
// (rv_version.hpp). A file with no `version` but the old `format = 1` header
// is read as version 0.0, the pre-version header kept compatible only while
// the PDK major is 0.

struct rv_scene_object {
    std::string id; // stable: the editor gives it and never changes it
    std::string name;
    int32_t parent = -1;                    // index into rv_scene::objects; -1 at the root
    std::string kind;                       // "group", "camera", "mesh", "quad", "billboard" or "volume"; empty: form 2
    std::string entity;                     // entity type name; empty: the base type
    rv_pdklib::fx_vec3 position{ 0, 0, 0 }; // Q20.12 scene units
    rv_pdklib::fx_vec3 rotation{ 0, 0, 0 }; // Q20.12 degrees; R = Ry * Rx * Rz; [x (pitch), y (yaw), z (roll)]
    rv_pdklib::fx_vec3 scale{ rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one }; // Q20.12, 1.0 is fx_one
    std::string mesh;                                     // a disc asset for kind "mesh"; empty: a unit cube
    std::string texture;                                  // a disc texture, or empty
    rv_pdklib::fx uv[4]{ 0, 0, 0, 0 };                    // Q20.12 quad/billboard texture rect, pixels: u0,v0,u1,v1
    rv_color tint{ 255, 255, 255 };                       // quad/billboard modulation
    rv_pdklib::fx tess = 2 * rv_pdklib::fx_one;           // Q20.12 quad/billboard subdivision density, 2.0
    std::vector<rv_pdklib::rv_manifest_tree_entry> extra; // keys the loader does not know, in file order
};

// A [[component]]: the index of its object, the component type's name, and the field values as the file gives them.
struct rv_scene_component {
    int32_t object = -1; // index into rv_scene::objects
    std::string name;
    std::vector<rv_pdklib::rv_manifest_tree_entry> fields; // in file order
};

struct rv_scene {
    uint32_t version_major = 0;
    uint32_t version_minor = 0;
    std::vector<rv_scene_object> objects;
    std::vector<rv_scene_component> components;                      // in file order
    std::string preamble;                                            // the comment lines before the first section, as written
    std::vector<rv_pdklib::rv_manifest_tree_entry> scene_extra;      // [scene] keys not read as version or format
    std::vector<rv_pdklib::rv_manifest_tree_section> other_sections; // sections other than [scene] and [[object]]
};

// 0 with the scene, or 1 with every problem in `error`, each stamped with
// `origin`: syntax, a missing/malformed/non-compatible version, a missing or
// repeated id, an unknown parent, a parent cycle, an unknown or missing kind on a form-1 object, a vector
// that is not three numbers, a malformed `uv`/`tint`/`tess`, a number outside Q20.12,
// a non-string `entity`,
// a [[component]] without a string `object` or `name`, one on an unknown object,
// or a name repeated on one object. Unknown keys are kept
// in `extra`/`scene_extra` and unknown sections in `other_sections`, each list in file
// order. A `format` integer is not kept: a write-back writes `version` only, as the editor does.
inline int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error);

// Writes `scene` back in the dialect rv_scene_parse reads, the inverse of it. A parent index
// outside objects is written as a root, a component on such an index is not written.
// A 0.0 scene is written as the PDK version.
inline std::string rv_scene_render(const rv_scene &scene);

// One value as rv_scene_render writes it: a quoted string, [a, b] for an array, the shortest fixed number.
inline std::string rv_scene_value_text(const rv_pdklib::rv_manifest_mvalue &value);

// Object `index`'s local transform: T * Ry * Rx * Rz * S.
inline rv_pdklib::rv_mat4 rv_scene_local(const rv_scene_object &object);

// Object `index`'s transform in scene space: its parents' applied outside its own.
inline rv_pdklib::rv_mat4 rv_scene_world(const rv_scene &scene, std::size_t index);

// A "quad" object's four scene-space corners, PDK Z order (top-left, top-right,
// bottom-left, bottom-right of its local XY square), for the game and the editor alike.
inline void rv_scene_quad_corners(const rv_scene &scene, std::size_t index, rv_pdklib::rv_vec3 out[4]);

} // namespace rv_pdkkit

#include "pdkkit/scene/rv_scene_read.hpp"
#include "pdkkit/scene/rv_scene_write.hpp"
