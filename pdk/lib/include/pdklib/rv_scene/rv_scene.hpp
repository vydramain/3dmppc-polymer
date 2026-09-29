#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_math/rv_math.hpp"

namespace rv_pdklib
{

// A scene document (editor/docs/adr/0009-scene-document.md) as a disc reads it:
// objects in file order with their parents resolved. It takes the file's BYTES,
// never a path: reading is the drive's (rv_cd_asset_*), as for rv_obj. A disc
// that draws no scene never calls it.
//
// [scene] carries version = "M.m", the PDK version the file was written
// against; a file is read when rv_version_compatible(major, minor) holds
// (rv_version.hpp). A file with no `version` but the old `format = 1` header
// is read as version 0.0, the pre-version header kept compatible only while
// the PDK major is 0.

struct rv_scene_object {
    std::string id;           // stable: the editor gives it and never changes it
    std::string name;
    int32_t parent = -1;      // index into rv_scene::objects; -1 at the root
    std::string kind;         // "group", "camera" or "mesh"
    rv_vec3 position{ 0.0f, 0.0f, 0.0f };
    rv_vec3 rotation{ 0.0f, 0.0f, 0.0f }; // degrees; R = Ry * Rx * Rz (yaw, pitch, roll)
    rv_vec3 scale{ 1.0f, 1.0f, 1.0f };
    std::string mesh;         // a disc asset for kind "mesh"; empty: a unit cube
    std::string texture;      // a disc texture, or empty
};

struct rv_scene {
    uint32_t version_major = 0;
    uint32_t version_minor = 0;
    std::vector<rv_scene_object> objects;
};

// 0 with the scene, or 1 with every problem in `error`, each stamped with
// `origin`: syntax, a missing/malformed/non-compatible version, a missing or
// repeated id, an unknown parent, a parent cycle, an unknown kind, a vector
// that is not three numbers. Keys the loader does not know are skipped.
int rv_scene_parse(const std::string &text, const std::string &origin, rv_scene &scene, std::string &error);

// Object `index`'s local transform: T * Ry * Rx * Rz * S.
rv_mat4 rv_scene_local(const rv_scene_object &object);

// Object `index`'s transform in scene space: its parents' applied outside its own.
rv_mat4 rv_scene_world(const rv_scene &scene, std::size_t index);

} // namespace rv_pdklib
