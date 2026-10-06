// The burner's map of source files to disc entries, read as it wrote it.

#include "build/rv_editor_build_map.hpp"

#include <fstream>

#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

namespace
{

// Map file format: source<tab>kind<tab>name[<tab>parameter], defined by rv_burner_map.cpp/hpp.
constexpr std::string_view map_file_extension = ".map";
constexpr std::string_view map_header_prefix = "mppcburner-map ";
constexpr char map_field_sep = '\t';
// Field count and indices in map line (tab-separated).
constexpr size_t map_field_count = 4;
constexpr size_t map_field_source = 0;
constexpr size_t map_field_kind = 1;
constexpr size_t map_field_name = 2;
constexpr size_t map_field_param = 3;

} // namespace

std::filesystem::path rv_editor_build_map_path(const std::filesystem::path &artifact)
{
    return artifact.string() + std::string(map_file_extension);
}

std::map<std::string, rv_editor_map_entry> rv_editor_build_map_read(const std::filesystem::path &path)
{
    std::map<std::string, rv_editor_map_entry> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    const std::string header = std::string(map_header_prefix) + rv_pdklib::rv_version_str;
    if (!std::getline(in, line) || line != header) {
        return out;
    }
    while (std::getline(in, line)) {
        std::string field[map_field_count];
        size_t at = 0;
        for (size_t i = 0; i < map_field_count && at <= line.size(); ++i) {
            const size_t tab = line.find(map_field_sep, at);
            field[i] = line.substr(at, tab == std::string::npos ? std::string::npos : tab - at);
            at = tab == std::string::npos ? line.size() + 1 : tab + 1;
        }
        if (!field[map_field_source].empty() && !field[map_field_name].empty()) {
            out[field[map_field_source]] = { field[map_field_kind], field[map_field_name], field[map_field_param] };
        }
    }
    return out;
}

} // namespace rv_editor
