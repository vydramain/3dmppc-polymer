// The project tree walk behind the name resolvers: meshes, textures and models are found by name.

#include "panes/rv_editor_walk.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

namespace rv_editor
{

namespace
{

// One full walk per second for the index, on the frame thread: a file dropped in or a duplicate added is
// seen within about a second, and a failed walk is not retried within that time either. Ceiling: past this
// rate, walk on a worker thread or watch the files.
constexpr std::chrono::seconds walk_ttl{ 1 };
// The walk does not enter a directory whose name starts with this: dotfiles and build outputs,
// and any other folder with such a prefix (build_assets too). Its files are not candidates.
constexpr std::string_view skip_build_dir_prefix = "build";
// One pattern list per rv_editor_name_section.
constexpr std::size_t section_count = 3;
static_assert(static_cast<std::size_t>(rv_editor_name_section::assets) == 0 &&
        static_cast<std::size_t>(rv_editor_name_section::textures) == 1 &&
        static_cast<std::size_t>(rv_editor_name_section::models) + 1 == section_count,
    "rv_editor_name_section must list the sections in this order, one per pattern list");

using section_patterns = std::array<std::vector<std::string>, section_count>;

// A file the walk accepted, with a bit per section whose patterns match it.
struct indexed_file {
    std::filesystem::path path;
    unsigned sections = 0;
};
using name_map = std::unordered_map<std::string, std::vector<indexed_file>>;

// The last walk: its root and pattern lists, when it ran, and its outcome. A failure is held like a result.
struct name_index {
    bool held = false;
    std::filesystem::path root;
    section_patterns patterns;
    std::chrono::steady_clock::time_point walked;
    std::error_code error; // the walk's failure; the maps are then empty
    name_map by_filename;
    name_map by_stem;
};

// File-local: the editor's UI thread is the only caller, so one index serves every resolver.
name_index disc_index;

bool walk_skip_dir(const std::string &name)
{
    return !name.empty() && (name[0] == '.' || name.starts_with(skip_build_dir_prefix));
}

// The project's pattern lists by reference, in section order: nothing is copied to compare them.
std::array<const std::vector<std::string> *, section_count> pattern_lists(const rv_editor_project &project)
{
    return { &project.assets_patterns, &project.textures_patterns, &project.models_patterns };
}

// True when the held walk is for this project's root and pattern lists.
bool held_for(const name_index &index, const rv_editor_project &project)
{
    if (!index.held || index.root != project.root) {
        return false;
    }
    const auto lists = pattern_lists(project);
    for (std::size_t s = 0; s < section_count; ++s) {
        if (index.patterns[s] != *lists[s]) {
            return false;
        }
    }
    return true;
}

unsigned section_mask(const section_patterns &patterns, const std::string &rel)
{
    unsigned mask = 0;
    for (std::size_t s = 0; s < section_count; ++s) {
        for (const std::string &pattern : patterns[s]) {
            if (rv_pdklib::rv_manifest_pattern_matches(pattern, rel)) {
                mask |= 1u << s;
                break;
            }
        }
    }
    return mask;
}

// Walks the project into `out`. Returns the walk's own error code; the caller discards `out` then.
std::error_code build_index(const rv_editor_project &project, const section_patterns &patterns, name_index &out)
{
    std::error_code ec;
    constexpr auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(project.root, opts, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code entry_ec;
        const bool is_dir = it->is_directory(entry_ec);
        if (entry_ec) {
            continue; // this one entry is unreadable or vanished; the walk goes on
        }
        if (is_dir) {
            if (walk_skip_dir(it->path().filename().string())) {
                it.disable_recursion_pending();
            }
            continue;
        }
        std::error_code rel_ec;
        const std::string rel = std::filesystem::relative(it->path(), project.root, rel_ec).generic_string();
        if (rel_ec) {
            continue; // no path relative to the root cannot match a pattern; the walk goes on
        }
        const unsigned sections = section_mask(patterns, rel);
        if (sections == 0) {
            continue;
        }
        const indexed_file file{ it->path(), sections };
        out.by_filename[it->path().filename().string()].push_back(file);
        out.by_stem[it->path().stem().string()].push_back(file);
    }
    return ec;
}

} // namespace

rv_editor_walk_result
rv_editor_walk_name(const rv_editor_project &project, rv_editor_name_section section, bool by_stem, const std::string &name)
{
    const auto now = std::chrono::steady_clock::now();
    if (!held_for(disc_index, project) || now - disc_index.walked >= walk_ttl) {
        name_index fresh;
        fresh.held = true;
        fresh.root = project.root;
        fresh.walked = now;
        const auto lists = pattern_lists(project);
        for (std::size_t s = 0; s < section_count; ++s) {
            fresh.patterns[s] = *lists[s];
        }
        fresh.error = build_index(project, fresh.patterns, fresh);
        if (fresh.error) {
            fresh.by_filename.clear();
            fresh.by_stem.clear();
        }
        disc_index = std::move(fresh);
    }
    rv_editor_walk_result result;
    if (disc_index.error) {
        result.ec = disc_index.error;
        return result;
    }
    const name_map &names = by_stem ? disc_index.by_stem : disc_index.by_filename;
    const auto hit = names.find(name);
    const unsigned bit = 1u << static_cast<unsigned>(section);
    if (hit != names.end()) {
        for (const indexed_file &file : hit->second) {
            if (file.sections & bit) {
                result.matches.push_back(file.path);
            }
        }
    }
    return result;
}

} // namespace rv_editor
