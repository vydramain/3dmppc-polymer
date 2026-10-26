#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace rv_editor
{

// The project's models folder: the folder of the first [assets] model pattern that is a literal folder plus
// "/*.gltf"; a first such pattern that is absolute or has "." or ".." parts is refused. RV_OK with `dir`;
// RV_ERR_INVAL with the reason otherwise.
int rv_editor_model_target_dir(const std::filesystem::path &root,
    const std::vector<std::string> &patterns,
    std::filesystem::path &dir,
    std::string &error);

// The pending folder of a models folder `models_dir` (no trailing separator): a sibling named "." + its name
// + "-pending". Being as deep as `models_dir`, a picture URI like "../tex/x.png" in a model written there
// still resolves after the model moves into `models_dir`.
std::filesystem::path rv_editor_model_pending_dir(const std::filesystem::path &models_dir);

// A free "<base>.gltf", else "<base>_<n>.gltf" (n from 2), into `name`. Free: no .gltf or .bin of the stem
// in `models_dir` nor in its pending folder. RV_OK; RV_ERR_INVAL for an empty base or one with a separator;
// RV_ERR_INVAL for a pending folder that is a symbolic link or not a folder; RV_ERR_IO when a check fails.
int rv_editor_model_free_name(const std::filesystem::path &models_dir,
    const std::string &base,
    std::string &name,
    std::string &error);

// Moves the pending .gltf files named in `current` (filenames such as "b.gltf"), with their .bin, into
// `models_dir`. Names only in `history` stay pending; the rest of the pending folder is deleted, and the
// folder too once empty. A target that exists (dangling links count), or one that appears during the moves,
// is RV_ERR_INVAL; a move that fails is RV_ERR_IO. Either way the moves done before it are undone.
// A pending folder that is a symbolic link is refused.
// RV_OK when there is no pending folder; RV_ERR_INVAL for a refusal (symbolic link, target taken),
// RV_ERR_IO for a failed move, check or delete.
int rv_editor_model_stage_settle(const std::filesystem::path &models_dir,
    const std::set<std::string> &current,
    const std::set<std::string> &history,
    std::string &error);

} // namespace rv_editor
