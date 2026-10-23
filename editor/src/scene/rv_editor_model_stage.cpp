#include "scene/rv_editor_model_stage.hpp"

#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Pending folder name is "." + models folder name + this.
constexpr std::string_view pending_suffix = "-pending";
constexpr std::string_view gltf_ext = ".gltf";
constexpr std::string_view bin_ext = ".bin";
// The pattern file name that marks a models folder.
constexpr std::string_view models_glob = "*.gltf";
// Characters that make a pattern's folder a glob rather than a literal folder.
constexpr std::string_view glob_chars = "*?[";
// Numbering of a copied model name starts here.
constexpr int first_copy_number = 2;
// A model name is a bare filename: no separator may appear in it.
constexpr std::string_view name_separators = "/\\";

// One file move; undone by renaming `to` back to `from`.
struct planned_move {
    std::filesystem::path from;
    std::filesystem::path to;
};

// Whether `file` is present; a dangling symbolic link is present too, so its name is taken.
int entry_present(const std::filesystem::path &file, bool &present, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::symlink_status(file, ec);
    if (st.type() == std::filesystem::file_type::not_found) {
        present = false;
        return RV_OK;
    }
    if (ec) {
        error = "cannot check " + file.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    present = true;
    return RV_OK;
}

// The pending folder: absent (present false), or a real folder. A symbolic link or a non-folder is refused.
int pending_state(const std::filesystem::path &pending, bool &present, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::symlink_status(pending, ec);
    if (st.type() == std::filesystem::file_type::not_found) {
        present = false;
        return RV_OK;
    }
    if (ec) {
        error = "cannot check " + pending.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (std::filesystem::is_symlink(st)) {
        error = "pending folder is a symbolic link, nothing touched: " + pending.string();
        return RV_ERR_INVAL;
    }
    if (!std::filesystem::is_directory(st)) {
        error = "pending path is not a folder: " + pending.string();
        return RV_ERR_INVAL;
    }
    present = true;
    return RV_OK;
}

// Whether `stem` is taken by a .gltf or .bin in `dir`.
int stem_in_dir(const std::filesystem::path &dir, const std::string &stem, bool &taken, std::string &error)
{
    taken = false;
    for (const std::string_view ext : { gltf_ext, bin_ext }) {
        const int rc = entry_present(dir / (stem + std::string(ext)), taken, error);
        if (rc != RV_OK || taken) {
            return rc;
        }
    }
    return RV_OK;
}

// Whether `stem` is taken in the models folder or in its pending folder.
int stem_taken(const std::filesystem::path &models_dir,
    const std::filesystem::path &pending,
    const std::string &stem,
    bool &taken,
    std::string &error)
{
    if (const int rc = stem_in_dir(models_dir, stem, taken, error); rc != RV_OK || taken) {
        return rc;
    }
    return stem_in_dir(pending, stem, taken, error);
}

// The stem of a .gltf or .bin filename without separators; empty for any other name.
std::string model_stem(const std::string &file)
{
    for (const std::string_view ext : { gltf_ext, bin_ext }) {
        if (file.size() > ext.size() && file.ends_with(ext) && file.find_first_of(name_separators) == std::string::npos) {
            return file.substr(0, file.size() - ext.size());
        }
    }
    return {};
}

// Stems of the names in `current` that are .gltf files waiting in `pending`.
int waiting_stems(const std::filesystem::path &pending,
    const std::set<std::string> &current,
    std::vector<std::string> &stems,
    std::string &error)
{
    for (const std::string &name : current) {
        const std::string stem = model_stem(name);
        if (stem.empty() || !name.ends_with(gltf_ext)) {
            continue;
        }
        bool waiting = false;
        if (const int rc = entry_present(pending / name, waiting, error); rc != RV_OK) {
            return rc;
        }
        if (waiting) {
            stems.push_back(stem);
        }
    }
    return RV_OK;
}

// Adds the moves of stem.gltf, and stem.bin when present, from `pending` into `models_dir`. A target that
// exists is RV_ERR_INVAL.
int plan_stem(const std::filesystem::path &pending,
    const std::filesystem::path &models_dir,
    const std::string &stem,
    std::vector<planned_move> &plan,
    std::string &error)
{
    for (const std::string_view ext : { gltf_ext, bin_ext }) {
        const std::filesystem::path source = pending / (stem + std::string(ext));
        bool present = false;
        if (const int rc = entry_present(source, present, error); rc != RV_OK) {
            return rc;
        }
        if (!present) {
            continue;
        }
        const std::filesystem::path target = models_dir / source.filename();
        if (const int rc = entry_present(target, present, error); rc != RV_OK) {
            return rc;
        }
        if (present) {
            error = "model " + source.filename().string() + " already exists in " + models_dir.string() + ": nothing moved";
            return RV_ERR_INVAL;
        }
        plan.push_back({ source, target });
    }
    return RV_OK;
}

// Renames `done` back, last first; a failure is added to `error`.
void move_back(const std::vector<planned_move> &done, std::string &error)
{
    for (auto it = done.rbegin(); it != done.rend(); ++it) {
        std::error_code ec;
        std::filesystem::rename(it->to, it->from, ec);
        if (ec) {
            error += "; cannot move back " + it->to.string() + ": " + ec.message();
        }
    }
}

// Renames each planned file in order; when one fails, the files already moved go back. The window between
// the target check and its rename is a race we accept: the editor is the only writer of the models folder.
int execute_moves(const std::vector<planned_move> &plan, std::string &error)
{
    std::vector<planned_move> done;
    for (const planned_move &move : plan) {
        // A target that appeared since planning is a clash: undo, refuse, touch nothing more.
        bool present = false;
        if (const int rc = entry_present(move.to, present, error); rc != RV_OK) {
            move_back(done, error);
            return rc;
        }
        if (present) {
            error = "target appeared, moves undone: " + move.to.string();
            move_back(done, error);
            return RV_ERR_INVAL;
        }
        std::error_code ec;
        std::filesystem::rename(move.from, move.to, ec);
        if (ec) {
            error = "cannot move " + move.from.string() + ": " + ec.message();
            move_back(done, error);
            return RV_ERR_IO;
        }
        done.push_back(move);
    }
    return RV_OK;
}

// Deletes what is left in `pending` except the files of `history` models, then the folder once empty.
int remove_left(const std::filesystem::path &pending, const std::set<std::string> &history, std::string &error)
{
    std::set<std::string> kept;
    for (const std::string &name : history) {
        kept.insert(model_stem(name));
    }
    kept.erase(std::string());
    std::vector<std::filesystem::path> doomed;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(pending, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string stem = model_stem(it->path().filename().string());
        if (stem.empty() || !kept.contains(stem)) {
            doomed.push_back(it->path());
        }
    }
    if (ec) {
        error = "cannot list " + pending.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    for (const std::filesystem::path &file : doomed) {
        std::filesystem::remove_all(file, ec);
        if (ec) {
            error = "cannot delete " + file.string() + ": " + ec.message();
            return RV_ERR_IO;
        }
    }
    const bool empty = std::filesystem::is_empty(pending, ec);
    if (ec) {
        error = "cannot check " + pending.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (!empty) {
        return RV_OK;
    }
    std::filesystem::remove(pending, ec);
    if (ec) {
        error = "cannot delete " + pending.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Whether `p` has a "." or ".." part.
bool has_dot_part(const std::filesystem::path &p)
{
    constexpr std::string_view self_part = ".";
    constexpr std::string_view parent_part = "..";
    for (const std::filesystem::path &part : p) {
        if (part == self_part || part == parent_part) {
            return true;
        }
    }
    return false;
}

} // namespace

int rv_editor_model_target_dir(const std::filesystem::path &root,
    const std::vector<std::string> &patterns,
    std::filesystem::path &dir,
    std::string &error)
{
    for (const std::string &pattern : patterns) {
        const std::filesystem::path path(pattern);
        const std::string folder = path.parent_path().generic_string();
        if (path.filename().string() != models_glob || folder.empty() ||
            folder.find_first_of(glob_chars) != std::string::npos) {
            continue;
        }
        if (path.is_absolute() || has_dot_part(path.parent_path())) {
            error = "models folder must be relative, without . or ..: " + pattern;
            return RV_ERR_INVAL;
        }
        dir = root / path.parent_path();
        return RV_OK;
    }
    error = "no [assets] pattern names a folder as \"<folder>/*.gltf\"";
    return RV_ERR_INVAL;
}

std::filesystem::path rv_editor_model_pending_dir(const std::filesystem::path &models_dir)
{
    return models_dir.parent_path() / ("." + models_dir.filename().string() + std::string(pending_suffix));
}

int rv_editor_model_free_name(const std::filesystem::path &models_dir,
    const std::string &base,
    std::string &name,
    std::string &error)
{
    if (base.empty() || base.find_first_of(name_separators) != std::string::npos) {
        error = "model name is empty or has a folder: '" + base + "'";
        return RV_ERR_INVAL;
    }
    const std::filesystem::path pending = rv_editor_model_pending_dir(models_dir);
    bool present = false;
    if (const int rc = pending_state(pending, present, error); rc != RV_OK) {
        return rc;
    }
    std::string stem = base;
    for (int n = first_copy_number;; ++n) {
        bool taken = false;
        if (const int rc = stem_taken(models_dir, pending, stem, taken, error); rc != RV_OK) {
            return rc;
        }
        if (!taken) {
            name = stem + std::string(gltf_ext);
            return RV_OK;
        }
        stem = base + "_" + std::to_string(n);
    }
}

int rv_editor_model_stage_settle(const std::filesystem::path &models_dir,
    const std::set<std::string> &current,
    const std::set<std::string> &history,
    std::string &error)
{
    const std::filesystem::path pending = rv_editor_model_pending_dir(models_dir);
    bool present = false;
    if (const int rc = pending_state(pending, present, error); rc != RV_OK) {
        return rc;
    }
    if (!present) {
        return RV_OK;
    }
    std::vector<std::string> stems;
    if (const int rc = waiting_stems(pending, current, stems, error); rc != RV_OK) {
        return rc;
    }
    // Every target is checked before the first move, so a clash leaves the pending folder whole.
    std::vector<planned_move> plan;
    for (const std::string &stem : stems) {
        if (const int rc = plan_stem(pending, models_dir, stem, plan, error); rc != RV_OK) {
            return rc;
        }
    }
    if (const int rc = execute_moves(plan, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = remove_left(pending, history, error); rc != RV_OK) {
        return rc;
    }
    return RV_OK;
}

} // namespace rv_editor
