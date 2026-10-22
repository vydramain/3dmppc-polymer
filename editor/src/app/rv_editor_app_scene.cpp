// The project's scene document: opened from scenes/, created there
// under a free name, saved back; the Scene layout's panes all show this one.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

#include "pdk/rv_err.h"

#include "panes/rv_editor_walk.hpp"
#include "project/rv_editor_manifest_edit.hpp"
#include "scene/rv_editor_model_stage.hpp"
#include "scene/rv_editor_scene.hpp"
#include "scene/rv_editor_scene_codegen.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Compares two paths: weakly_canonical both, fallback to lexically_normal on error.
bool rv_editor_same_path(const std::filesystem::path &a, const std::filesystem::path &b)
{
    std::error_code ec_a;
    std::error_code ec_b;
    std::filesystem::path canonical_a = std::filesystem::weakly_canonical(a, ec_a);
    std::filesystem::path canonical_b = std::filesystem::weakly_canonical(b, ec_b);
    if (ec_a || ec_b) {
        canonical_a = a.lexically_normal();
        canonical_b = b.lexically_normal();
    }
    return canonical_a == canonical_b;
}

// Suffix for scene file names.
constexpr std::string_view scene_suffix = ".scene.toml";

// Bytes of "<name>.scene.toml" with its NUL: the old console's limit, kept as an editor convention.
// Neither the new console nor pdkkit checks scene name length. Kept while the scene codegen emits
// the old-style code and while consoles older than the scene's removal from the contract still run.
constexpr size_t scene_file_capacity = 64;
constexpr size_t scene_name_max = scene_file_capacity - 1 - scene_suffix.size();

// Numbering of a copied scene name starts here.
constexpr int first_copy_number = 2;

// Project's scenes directory.
constexpr std::string_view scenes_dir = "scenes";

// First name for a new scene.
constexpr std::string_view main_scene_name = "main";

// Prefix for numbered scene copies.
constexpr std::string_view scene_num_prefix = "scene";

// Section name in disc.toml for scene assets.
constexpr std::string_view assets_section = "assets";

// Glob pattern for scene files in the assets section.
constexpr std::string_view scenes_glob_pattern = "scenes/*.scene.toml";

std::string rv_editor_scene_label(const rv_editor_app &app, const std::filesystem::path &path)
{
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(path, app.project.root, ec);
    return (ec || rel.empty() ? path : rel).string();
}

// Adds the mesh names of `objects` to `names`.
void rv_editor_app_add_mesh_names(const std::vector<rv_editor_scene_object> &objects, std::set<std::string> &names)
{
    for (const rv_editor_scene_object &o : objects) {
        if (rv_editor_object_get_kind(o) == kind_mesh) {
            names.insert(rv_editor_object_get_mesh(o));
        }
    }
}

// The mesh names of the scene as it is (`current`) and of its undo and redo snapshots (`history`).
void rv_editor_app_scene_names(const rv_editor_scene_doc &doc, std::set<std::string> &current, std::set<std::string> &history)
{
    rv_editor_app_add_mesh_names(doc.scene.objects, current);
    for (const std::vector<rv_editor_scene_object> &snapshot : doc.undo) {
        rv_editor_app_add_mesh_names(snapshot, history);
    }
    for (const std::vector<rv_editor_scene_object> &snapshot : doc.redo) {
        rv_editor_app_add_mesh_names(snapshot, history);
    }
}

// Moves the pending models named in `current` into the project's models folder. Names only in `history` stay
// pending; the rest of the pending folder is deleted.
int rv_editor_app_models_settle(const rv_editor_app &app,
    const std::set<std::string> &current,
    const std::set<std::string> &history,
    std::string &error)
{
    std::filesystem::path models_dir;
    if (rv_editor_model_target_dir(app.project.root, app.project.models_patterns, models_dir, error) != RV_OK) {
        error.clear(); // no models folder: no pending one to move
        return RV_OK;
    }
    const int rc = rv_editor_model_stage_settle(models_dir, current, history, error);
    rv_editor_walk_reset(); // a failed settle may have moved some models already: the index is stale either way
    return rc;
}

} // namespace

size_t rv_editor_app_scene_name_max()
{
    return scene_name_max;
}

bool rv_editor_app_scene_name_valid(std::string_view name)
{
    if (name.empty() || name.size() > scene_name_max) {
        return false;
    }
    for (const char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

std::vector<std::filesystem::path> rv_editor_app_scene_files(const rv_editor_app &app)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(app.project.root / scenes_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > scene_suffix.size() && name.ends_with(scene_suffix)) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool rv_editor_app_scene_dirty(const rv_editor_app &app)
{
    return app.scene != nullptr && app.scene->dirty;
}

std::string rv_editor_app_scene_name(const rv_editor_app &app)
{
    return app.scene == nullptr ? std::string() : rv_editor_scene_label(app, app.scene->scene.path);
}

int rv_editor_app_scene_close(rv_editor_app &app)
{
    if (app.scene == nullptr) {
        return RV_OK;
    }
    // Empty names, also on discard: the saved file's models were moved by its save, so it needs none pending, and
    // the undo and redo names die with the scene. A failure is a warning; the close goes on.
    const std::set<std::string> none;
    std::string error;
    const int rc = rv_editor_app_models_settle(app, none, none, error);
    if (rc != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "pending models not cleaned: " + error);
    }
    app.scene.reset();
    return RV_OK;
}

int rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path)
{
    const bool dirty = rv_editor_app_scene_dirty(app);
    if (dirty && rv_editor_same_path(app.scene->scene.path, path)) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "scene already open: " + rv_editor_app_scene_name(app));
        return RV_ERR_ALREADY; // already open, with its edits: nothing done
    }
    if (dirty) {
        const std::string current_label = rv_editor_app_scene_name(app);
        const std::string other_label = rv_editor_scene_label(app, path);
        app.scene_error = current_label +
            " has unsaved changes: save it (Scene > Save Scene) or undo them "
            "before opening " +
            other_label;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.scene_error);
        return RV_ERR_BUSY;
    }
    rv_editor_scene scene;
    std::string error;
    const int load_code = rv_editor_scene_load(path, scene, error);
    if (load_code != RV_OK) {
        app.scene_error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not opened: " + error);
        return load_code;
    }
    auto doc = std::make_unique<rv_editor_scene_doc>();
    doc->scene = std::move(scene);
    (void)rv_editor_app_scene_close(app); // the clean scene it replaces: its leftover pending models go
    app.scene = std::move(doc);
    app.scene_error.clear();
    app.scene_move_note.clear();
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "scene opened: " + rv_editor_app_scene_name(app) +
            (app.scene->scene.read_only.empty() ? "" : " (read-only: " + app.scene->scene.read_only + ")"));
    return RV_OK;
}

std::string rv_editor_app_scene_free_name(const rv_editor_app &app)
{
    // A free name: an existing file is never written over.
    std::string name = std::string(main_scene_name);
    std::error_code ec;
    std::string suffix_str(scene_suffix);
    for (int n = first_copy_number; std::filesystem::exists(app.project.root / scenes_dir / (name + suffix_str), ec); ++n) {
        name = std::string(scene_num_prefix) + std::to_string(n);
    }
    return name;
}

int rv_editor_app_scene_create(rv_editor_app &app, std::string_view name, bool write_cpp, std::string &error)
{
    if (rv_editor_app_scene_dirty(app)) {
        error = rv_editor_app_scene_name(app) +
            " has unsaved changes: save it (Scene > Save Scene) or undo "
            "them before creating a new scene";
        return RV_ERR_INVAL;
    }
    if (!rv_editor_app_scene_name_valid(name)) {
        const size_t max_bytes = scene_name_max;
        error = rv_editor_text_format("scene_edit.invalid_scene_name", std::make_format_args(max_bytes));
        return RV_ERR_INVAL;
    }
    const std::string rel = std::string(scenes_dir) + "/" + std::string(name) + std::string(scene_suffix);
    const std::filesystem::path path = app.project.root / rel;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        error = rel + " already exists";
        return RV_ERR_INVAL;
    }

    const rv_editor_scene scene = rv_editor_scene_make(path);
    const int save_code = rv_editor_scene_save(scene, error);
    if (save_code != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not created: " + error);
        return save_code;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "created " + rel);

    // Whatever fails below, the scene file stays and is opened anyway;
    // only the last reason is reported back.
    int result = RV_OK;
    std::string reason;
    if (!rv_editor_project_on_disc(app.project, rel)) {
        std::string add_error;
        const int add_code =
            rv_editor_manifest_add_pattern(app.project.manifest, assets_section, scenes_glob_pattern, add_error);
        if (add_code == RV_OK) {
            rv_editor_project_reload_manifest(app.project);
            app.log.add(rv_editor_log_source::editor,
                rv_editor_log_level::info,
                "disc.toml: added scenes/*.scene.toml to [assets]");
        } else {
            result = add_code;
            reason = "not added to disc.toml: " + add_error;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, reason);
        }
    }
    if (write_cpp && app.project.has_build_section) {
        std::filesystem::path written;
        std::string cpp_error;
        const int codegen_code = rv_editor_scene_codegen_write(app.project.root, name, written, cpp_error);
        if (codegen_code == RV_OK) {
            app.log.add(rv_editor_log_source::editor,
                rv_editor_log_level::info,
                "wrote " + rv_editor_scene_label(app, written));
        } else {
            result = codegen_code;
            reason = "C++ not written: " + cpp_error;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, reason);
        }
    }

    rv_editor_app_scene_open(app, path);
    if (result != RV_OK) {
        error = reason;
    }
    return result;
}

int rv_editor_app_scene_save(rv_editor_app &app, std::string &error)
{
    if (!rv_editor_app_scene_dirty(app)) {
        return RV_ERR_ALREADY; // nothing unsaved: nothing written
    }
    // Models are moved before the scene is written; if the write then fails, the moved models stay in the models folder.
    std::set<std::string> current;
    std::set<std::string> history;
    rv_editor_app_scene_names(*app.scene, current, history);
    std::string stage_error;
    const int stage_code = rv_editor_app_models_settle(app, current, history, stage_error);
    if (stage_code != RV_OK) {
        error = "scene not saved: " + stage_error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, error);
        return RV_ERR_IO; // never RV_ERR_ALREADY: shell_save reads that as nothing to save
    }
    const int code = rv_editor_scene_save(app.scene->scene, error);
    if (code != RV_OK) {
        return code;
    }
    app.scene->dirty = false;
    app.scene_error.clear();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "saved " + rv_editor_app_scene_name(app));
    return RV_OK;
}

int rv_editor_app_scene_first(rv_editor_app &app)
{
    // rv_editor_app_open closed the old scene before its root changed, so app.scene is null here and this does nothing;
    // a scene left open here would clean the new project's models folder.
    (void)rv_editor_app_scene_close(app);
    app.scene_error.clear();
    app.scene_move_note.clear();
    const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
    if (files.empty()) {
        return RV_ERR_NOENT; // no scene in the project: nothing opened, the caller decides whether to say so
    }
    return rv_editor_app_scene_open(app, files.front());
}

int rv_editor_app_scene_move(rv_editor_app &app, const std::string &id, const std::string &parent, bool keep_world)
{
    if (app.scene == nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not moved: no scene open");
        return RV_ERR_INVAL;
    }
    std::string why;
    const int code = rv_editor_scene_reparent(*app.scene, id, parent, keep_world, why);
    if (code != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "not moved: " + why);
        app.scene_move_note = rv_editor_text_format("pane_hierarchy.not_moved", std::make_format_args(why));
        return code;
    }
    app.scene_move_note.clear();
    return RV_OK;
}

} // namespace rv_editor
