#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdkkit
{

// A scene document as a disc reads it:
// objects in file order with their parents resolved. It takes the file's BYTES,
// never a path: reading is the drive's (rv_cd_asset_*). A disc that draws no
// scene never calls it.

struct rv_scene_object {
    std::string id;       // stable: the editor gives it and never changes it
    int32_t parent = -1;  // index into rv_scene::objects; -1 at the root
    std::string entity;   // entity type name; empty: the base type
    std::vector<rv_pdklib::rv_manifest_tree_entry> extra; // keys the loader does not know, in file order
};

// A [[component]]: the index of its object, the component type's name, and the field values as the file gives them.
struct rv_scene_component {
    int32_t object = -1; // index into rv_scene::objects
    std::string name;
    std::vector<rv_pdklib::rv_manifest_tree_entry> fields; // in file order
};

struct rv_scene {
    std::vector<rv_scene_object> objects;
    std::vector<rv_scene_component> components;                      // in file order
    std::string preamble;                                            // the comment lines before the first section, as written
    std::vector<rv_pdklib::rv_manifest_tree_entry> scene_extra;      // all [scene] keys, in file order: the reader knows none
    std::vector<rv_pdklib::rv_manifest_tree_section> other_sections; // sections other than [scene] and [[object]]
};

// 0 with the scene, or 1 with every problem in `error`, each stamped with
// `origin`: syntax, a missing or repeated id, an unknown parent, a parent cycle,
// an object in form 1 (a key of rv_scene_form1_keys: its data belongs in [[component]] blocks), a non-string `entity`,
// a [[component]] without a string `object` or `name`, one on an unknown object,
// or a name repeated on one object. Unknown keys are kept in `extra`/`scene_extra`
// and unknown sections in `other_sections`, each list in file order.
inline int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error);

// Writes `scene` back in the dialect rv_scene_parse reads, the inverse of it. A parent index
// outside objects is written as a root, a component on such an index is not written.
inline std::string rv_scene_render(const rv_scene &scene);

// One value as rv_scene_render writes it: a quoted string, [a, b] for an array, the shortest fixed number.
inline std::string rv_scene_value_text(const rv_pdklib::rv_manifest_mvalue &value);

} // namespace rv_pdkkit

#include "pdkkit/scene/rv_scene_read.hpp"
#include "pdkkit/scene/rv_scene_write.hpp"
