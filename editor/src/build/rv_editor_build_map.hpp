#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace rv_editor
{

// One line of the burner's map: what a
// source file became on the disc. The editor reads names here and never works
// them out itself.
struct rv_editor_map_entry
{
    std::string kind;      // code, file, texture, entry, module
    std::string name;      // the disc entry's name
    std::string parameter; // a texture's format, a module's require name
};

// Where mppcburner --map writes the map of the build or image at `artifact`.
std::filesystem::path rv_editor_build_map_path(const std::filesystem::path &artifact);

// The map by source path, relative to the project root; empty when the file is
// missing or the map's first line is not "mppcburner-map <PDK version>".
std::map<std::string, rv_editor_map_entry> rv_editor_build_map_read(const std::filesystem::path &path);

} // namespace rv_editor
