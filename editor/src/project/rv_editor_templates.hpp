#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rv_editor
{

// The starting discs New Project copies (TPL-01): mppcdiscs/example-cpp and
// example-lua of this repository, found at run time the way the icons are.
struct rv_editor_template
{
    std::string id;          // "example-lua": its directory and disc id
    std::string name;        // "Minimal Lua"
    std::string description; // what it holds and what it shows
    std::filesystem::path dir;
};

std::vector<rv_editor_template> rv_editor_templates();

// What New Project asks (TPL-02).
struct rv_editor_new_project
{
    std::string name;             // the disc's title
    std::string disc_id;          // its id: the directory's name too
    std::filesystem::path parent; // where the directory goes
    size_t template_index = 0;
};

// "my-game" from "My Game!": lowercase letters, digits and dashes.
std::string rv_editor_disc_id_from(const std::string &name);

// The New Project field a problem is about; root is <parent>/<disc id> itself.
enum class rv_editor_new_project_field
{
    none,
    name,
    disc_id,
    parent,
    template_index,
    root,
};

// Why `p` cannot be created as it stands, and in *field which field that is about;
// empty when it can. The disc id itself is judged by pdklib's manifest rules once written.
std::string rv_editor_new_project_problem(const rv_editor_new_project &p, const std::vector<rv_editor_template> &templates,
    rv_editor_new_project_field *field = nullptr);

// The files the new project will have, relative to its root, sorted: the preview.
std::vector<std::string> rv_editor_new_project_files(const rv_editor_new_project &p, const rv_editor_template &t);

// Copies the template into <parent>/.<id>.creating with its names and ids
// replaced, checks the manifest with pdklib, then renames it into place. Nothing
// existing is written over. False with the reason; a failed attempt leaves nothing.
bool rv_editor_new_project_create(const rv_editor_new_project &p, const rv_editor_template &t, std::string &error);

// The projects opened lately, newest first (PRJ-07), kept one path a line in
// $XDG_CONFIG_HOME/3dmppc-editor/recent. Removing one only forgets it.
std::vector<std::filesystem::path> rv_editor_recent_load();
void rv_editor_recent_add(const std::filesystem::path &root);
void rv_editor_recent_remove(const std::filesystem::path &root);

} // namespace rv_editor
