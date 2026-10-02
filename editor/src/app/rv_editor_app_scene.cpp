// The project's scene document: opened from scenes/, created there
// under a free name, saved back; the Scene layout's panes all show this one.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "project/rv_editor_manifest_edit.hpp"
#include "scene/rv_editor_scene_codegen.hpp"

namespace rv_editor
{

namespace
{

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
    for (std::filesystem::directory_iterator it(app.project.root / "scenes", ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > 11 && name.ends_with(".scene.toml")) {
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

void rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path)
{
    if (rv_editor_app_scene_dirty(app)) {
        std::error_code ec_a;
        std::error_code ec_b;
        std::filesystem::path current = std::filesystem::weakly_canonical(app.scene->scene.path, ec_a);
        std::filesystem::path other = std::filesystem::weakly_canonical(path, ec_b);
        if (ec_a || ec_b) {
            current = app.scene->scene.path.lexically_normal();
            other = path.lexically_normal();
        }
        if (current == other) {
            return; // already open, with its edits
        }
        const std::string current_label = rv_editor_app_scene_name(app);
        const std::string other_label = rv_editor_scene_label(app, path);
        app.scene_error = current_label + " has unsaved changes: save it (Scene > Save Scene) or undo them "
                                           "before opening " + other_label;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.scene_error);
        return;
    }
    rv_editor_scene scene;
    std::string error;
    if (!rv_editor_scene_load(path, scene, error)) {
        app.scene_error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not opened: " + error);
        return;
    }
    auto doc = std::make_unique<rv_editor_scene_doc>();
    doc->scene = std::move(scene);
    app.scene = std::move(doc);
    app.scene_error.clear();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "scene opened: " + rv_editor_app_scene_name(app) +
            (app.scene->scene.read_only.empty() ? "" : " (read-only: " + app.scene->scene.read_only + ")"));
}

std::string rv_editor_app_scene_free_name(const rv_editor_app &app)
{
    // A free name: an existing file is never written over.
    std::string name = "main";
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(app.project.root / "scenes" / (name + ".scene.toml"), ec); ++n) {
        name = "scene" + std::to_string(n);
    }
    return name;
}

bool rv_editor_app_scene_create(rv_editor_app &app, std::string_view name, bool write_cpp, std::string &error)
{
    if (rv_editor_app_scene_dirty(app)) {
        error = rv_editor_app_scene_name(app) + " has unsaved changes: save it (Scene > Save Scene) or undo "
                                                 "them before creating a new scene";
        return false;
    }
    if (!rv_editor_scene_name_valid(name)) {
        error = "name must be non-empty letters, digits, _ or -";
        return false;
    }
    const std::string rel = "scenes/" + std::string(name) + ".scene.toml";
    const std::filesystem::path path = app.project.root / rel;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        error = rel + " already exists";
        return false;
    }

    const rv_editor_scene scene = rv_editor_scene_make(path);
    if (!rv_editor_scene_save(scene, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not created: " + error);
        return false;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "created " + rel);

    // Whatever fails below, the scene file stays and is opened anyway;
    // only the last reason is reported back.
    bool ok = true;
    std::string reason;
    if (!rv_editor_project_on_disc(app.project, rel)) {
        std::string add_error;
        if (rv_editor_manifest_add_pattern(app.project.manifest, "assets", "scenes/*.scene.toml", add_error)) {
            rv_editor_project_reload_manifest(app.project);
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
                "disc.toml: added scenes/*.scene.toml to [assets]");
        } else {
            ok = false;
            reason = "not added to disc.toml: " + add_error;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, reason);
        }
    }
    if (write_cpp && app.project.has_build_section) {
        std::filesystem::path written;
        std::string cpp_error;
        if (rv_editor_scene_codegen_write(app.project.root, name, written, cpp_error)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
                "wrote " + rv_editor_scene_label(app, written));
        } else {
            ok = false;
            reason = "C++ not written: " + cpp_error;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, reason);
        }
    }

    rv_editor_app_scene_open(app, path);
    if (!ok) {
        error = reason;
    }
    return ok;
}

bool rv_editor_app_scene_save(rv_editor_app &app, std::string &error)
{
    if (!rv_editor_app_scene_dirty(app)) {
        return true;
    }
    if (!rv_editor_scene_save(app.scene->scene, error)) {
        return false;
    }
    app.scene->dirty = false;
    app.scene_error.clear();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "saved " + rv_editor_app_scene_name(app));
    return true;
}

void rv_editor_app_scene_first(rv_editor_app &app)
{
    app.scene.reset();
    app.scene_error.clear();
    const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
    if (!files.empty()) {
        rv_editor_app_scene_open(app, files.front());
    }
}

} // namespace rv_editor
