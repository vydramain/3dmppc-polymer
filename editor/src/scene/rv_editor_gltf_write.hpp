#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"

namespace rv_editor
{

// Writes the triangles `tris` (indices into model.triangles) as glTF 2.0 Separate: `gltf` (JSON) and
// `<stem>.bin` beside it. Each part keeps its colour; `texture_uri` maps a part's texture name to the
// URI of its PNG, relative to the directory of `gltf`. z is written negated, the reader's inverse.
// A part with no selected triangle is not written, so the file can hold fewer parts than the model.
// Both targets must be new: the pair of renames is not atomic, so an old pair could be left half replaced.
// URIs are written as given and must be plain: no '%', no ':', no leading '/'.
// Returns RV_OK, or RV_ERR_INVAL / RV_ERR_IO with `error` set to one sentence naming the cause.
// Transitional until E12c-0a2 moves its callers to the integer overload below; the float overload stays until then.
int rv_editor_gltf_write(const std::filesystem::path &gltf,
    const rv_pdklib::rv_mppcmod &model,
    const std::vector<uint32_t> &tris,
    const std::map<std::string, std::string> &texture_uri,
    std::string &error);

// The same write for an integer model. Checks it with rv_mppcmod_int_validate, then writes its float view.
int rv_editor_gltf_write(const std::filesystem::path &gltf,
    const rv_pdklib::rv_mppcmod_int_model &model,
    const std::vector<uint32_t> &tris,
    const std::map<std::string, std::string> &texture_uri,
    std::string &error);

} // namespace rv_editor
