// The project's scene document (ADR 0009): opened from scenes/, created there
// under a free name, saved back; the Scene layout's panes all show this one.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <system_error>

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

void rv_editor_app_scene_create(rv_editor_app &app)
{
    // A free name: an existing file is never written over.
    std::filesystem::path path = app.project.root / "scenes" / "main.scene.toml";
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(path, ec); ++n) {
        path = app.project.root / "scenes" / ("scene" + std::to_string(n) + ".scene.toml");
    }
    const rv_editor_scene scene = rv_editor_scene_make(path);
    std::string error;
    if (!rv_editor_scene_save(scene, error)) {
        app.scene_error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not created: " + error);
        return;
    }
    rv_editor_app_scene_open(app, path);
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
