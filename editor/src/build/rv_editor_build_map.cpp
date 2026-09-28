// The burner's map of source files to disc entries, read as it wrote it.

#include "build/rv_editor_build_map.hpp"

#include <fstream>

#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

std::filesystem::path rv_editor_build_map_path(const std::filesystem::path &artifact)
{
    return artifact.string() + ".map";
}

std::map<std::string, rv_editor_map_entry> rv_editor_build_map_read(const std::filesystem::path &path)
{
    std::map<std::string, rv_editor_map_entry> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    const std::string header = std::string("mppcburner-map ") + rv_pdklib::rv_version_str;
    if (!std::getline(in, line) || line != header) {
        return out;
    }
    while (std::getline(in, line)) {
        std::string field[4];
        size_t at = 0;
        for (int i = 0; i < 4 && at <= line.size(); ++i) {
            const size_t tab = line.find('\t', at);
            field[i] = line.substr(at, tab == std::string::npos ? std::string::npos : tab - at);
            at = tab == std::string::npos ? line.size() + 1 : tab + 1;
        }
        if (!field[0].empty() && !field[2].empty()) {
            out[field[0]] = { field[1], field[2], field[3] };
        }
    }
    return out;
}

} // namespace rv_editor
