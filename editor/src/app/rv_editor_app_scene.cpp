// The project's scene document: opened from scenes/, created there
// under a free name, saved back; the Scene layout's panes all show this one.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "pdk/rv_err.h"

#include "project/rv_editor_manifest_edit.hpp"
#include "scene/rv_editor_scene_codegen.hpp"

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

// Letters, digits, '_' or '-', non-empty: what scenes/<name>.scene.toml and the
// generated header's identifier both need to start from.
bool rv_editor_scene_name_valid(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

} // namespace

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

int rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path)
{
    const bool dirty = rv_editor_app_scene_dirty(app);
    if (dirty && rv_editor_same_path(app.scene->scene.path, path)) {
        return RV_OK; // already open, with its edits
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
    app.scene = std::move(doc);
    app.scene_error.clear();
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
    if (!rv_editor_scene_name_valid(name)) {
        error = "name must be non-empty letters, digits, _ or -";
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
        return RV_OK;
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
    app.scene.reset();
    app.scene_error.clear();
    const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
    if (files.empty()) {
        return RV_OK;
    }
    return rv_editor_app_scene_open(app, files.front());
}

} // namespace rv_editor
