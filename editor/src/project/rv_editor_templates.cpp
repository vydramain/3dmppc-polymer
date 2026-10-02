// New Project from the starting discs, and the list of recent projects.

#include "project/rv_editor_templates.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "layout/rv_editor_tile.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_editor
{

namespace
{

constexpr size_t rv_editor_recent_max = 10;

std::filesystem::path rv_editor_recent_path()
{
    const std::filesystem::path layout = rv_editor_layout_file_path();
    return layout.empty() ? layout : layout.parent_path() / "recent";
}

void rv_editor_recent_save(const std::vector<std::filesystem::path> &list)
{
    const std::filesystem::path path = rv_editor_recent_path();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path.string() + ".tmp", std::ios::binary | std::ios::trunc);
    for (const std::filesystem::path &p : list) {
        out << p.string() << "\n";
    }
    out.close();
    if (out) {
        std::filesystem::rename(path.string() + ".tmp", path, ec);
    }
}

// The template's name in a file or the text, replaced by the new id: "example-lua"
// and the namespace spelling "example_lua".
std::string rv_editor_template_rename(std::string s, const std::string &from, const std::string &to)
{
    std::string from_ns = from;
    std::string to_ns = to;
    std::replace(from_ns.begin(), from_ns.end(), '-', '_');
    std::replace(to_ns.begin(), to_ns.end(), '-', '_');
    for (const auto &[a, b] : { std::pair{ from, to }, std::pair{ from_ns, to_ns } }) {
        for (size_t at = s.find(a); at != std::string::npos; at = s.find(a, at + b.size())) {
            s.replace(at, a.size(), b);
        }
    }
    return s;
}

bool rv_editor_is_text(const std::filesystem::path &p)
{
    const std::string ext = p.extension().string();
    return ext == ".toml" || ext == ".md" || ext == ".lua" || ext == ".cpp" || ext == ".hpp" || ext == ".h" ||
        ext == ".c" || ext == ".txt";
}

} // namespace

std::vector<rv_editor_template> rv_editor_templates()
{
    const std::filesystem::path root = RV_EDITOR_TEMPLATE_DIR;
    return {
        { "example-cpp", "Minimal C++", "A C++ disc: a sprite grid and a line of text, drawn by its own code. "
                                        "Build, then restart to see a change.",
            root / "example-cpp" },
        { "example-lua", "Minimal Lua", "A Lua entry script over a small C++ bridge: persistent state and "
                                        "reload while it runs. It still needs the C++ compiler for the bridge.",
            root / "example-lua" },
    };
}

std::string rv_editor_disc_id_from(const std::string &name)
{
    std::string id;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) && u < 0x80) {
            id += static_cast<char>(std::tolower(u));
        } else if (!id.empty() && id.back() != '-') {
            id += '-';
        }
    }
    while (!id.empty() && id.back() == '-') {
        id.pop_back();
    }
    return id;
}

std::string rv_editor_new_project_problem(const rv_editor_new_project &p, const std::vector<rv_editor_template> &templates,
    rv_editor_new_project_field *field)
{
    rv_editor_new_project_field unused = rv_editor_new_project_field::none;
    rv_editor_new_project_field &at = field != nullptr ? *field : unused;
    at = rv_editor_new_project_field::name;
    if (p.name.empty()) {
        return "Give the project a name";
    }
    at = rv_editor_new_project_field::disc_id;
    if (p.disc_id.empty()) {
        return "Give the disc an id";
    }
    // The id names the templates' C++ namespace too, which cannot start with a digit.
    if (!std::isalpha(static_cast<unsigned char>(p.disc_id.front()))) {
        return "A disc id starts with a letter";
    }
    at = rv_editor_new_project_field::parent;
    if (p.parent.empty()) {
        return "Choose the directory the project goes in";
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(p.parent, ec)) {
        return p.parent.string() + " is not a directory";
    }
    at = rv_editor_new_project_field::template_index;
    if (p.template_index >= templates.size()) {
        return "Choose a template";
    }
    if (!std::filesystem::is_directory(templates[p.template_index].dir, ec)) {
        return "The template is missing: " + templates[p.template_index].dir.string();
    }
    at = rv_editor_new_project_field::root;
    if (std::filesystem::exists(p.parent / p.disc_id, ec)) {
        return (p.parent / p.disc_id).string() + " exists already: nothing is written over";
    }
    at = rv_editor_new_project_field::none;
    return {};
}

std::vector<std::string> rv_editor_new_project_files(const rv_editor_new_project &p, const rv_editor_template &t)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(t.dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            const std::string rel = std::filesystem::relative(it->path(), t.dir, ec).generic_string();
            out.push_back(rv_editor_template_rename(rel, t.id, p.disc_id.empty() ? t.id : p.disc_id));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool rv_editor_new_project_create(const rv_editor_new_project &p, const rv_editor_template &t, std::string &error)
{
    const std::vector<rv_editor_template> all = rv_editor_templates();
    error = rv_editor_new_project_problem(p, all);
    if (!error.empty()) {
        return false;
    }
    const std::filesystem::path staging = p.parent / ("." + p.disc_id + ".creating");
    const std::filesystem::path root = p.parent / p.disc_id;
    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    const auto fail = [&](const std::string &why) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        error = why;
        return false;
    };
    for (std::filesystem::recursive_directory_iterator it(t.dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string rel = std::filesystem::relative(it->path(), t.dir, ec).generic_string();
        const std::filesystem::path dst = staging / rv_editor_template_rename(rel, t.id, p.disc_id);
        std::filesystem::create_directories(dst.parent_path(), ec);
        if (ec) {
            return fail(dst.parent_path().string() + ": " + ec.message());
        }
        if (!rv_editor_is_text(it->path())) {
            std::filesystem::copy_file(it->path(), dst, ec);
            if (ec) {
                return fail(dst.string() + ": " + ec.message());
            }
            continue;
        }
        std::ifstream in(it->path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string body = rv_editor_template_rename(text.str(), t.id, p.disc_id);
        if (it->path().filename() == "disc.toml") {
            // The title line is the project's name.
            const size_t at = body.find("title = \"");
            if (at != std::string::npos) {
                const size_t end_quote = body.find('"', at + 9);
                std::string title = p.name;
                std::erase(title, '"');
                body.replace(at + 9, end_quote - (at + 9), title);
            }
        }
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        out << body;
        out.close();
        if (!out) {
            return fail("cannot write " + dst.string());
        }
    }
    if (ec) {
        return fail(t.dir.string() + ": " + ec.message());
    }
    // The rules for an id are pdklib's, not the editor's.
    rv_pdklib::rv_manifest manifest;
    std::string why;
    if (rv_pdklib::rv_manifest_load((staging / "disc.toml").string(), manifest, why) != 0 ||
        !rv_pdklib::rv_manifest_validate(manifest, why)) {
        return fail("the new disc.toml is not valid: " + why);
    }
    std::filesystem::rename(staging, root, ec);
    if (ec) {
        return fail("cannot move the new project into place: " + ec.message());
    }
    return true;
}

std::vector<std::filesystem::path> rv_editor_recent_load()
{
    std::vector<std::filesystem::path> list;
    std::ifstream in(rv_editor_recent_path());
    std::string line;
    while (std::getline(in, line) && list.size() < rv_editor_recent_max) {
        if (!line.empty()) {
            list.emplace_back(line);
        }
    }
    return list;
}

void rv_editor_recent_add(const std::filesystem::path &root)
{
    std::vector<std::filesystem::path> list = rv_editor_recent_load();
    std::erase(list, root);
    list.insert(list.begin(), root);
    if (list.size() > rv_editor_recent_max) {
        list.resize(rv_editor_recent_max);
    }
    rv_editor_recent_save(list);
}

size_t rv_editor_recent_remove(const std::filesystem::path &root)
{
    std::vector<std::filesystem::path> list = rv_editor_recent_load();
    const size_t index = static_cast<size_t>(std::find(list.begin(), list.end(), root) - list.begin());
    std::erase(list, root);
    rv_editor_recent_save(list);
    return index;
}

void rv_editor_recent_restore(const std::filesystem::path &root, size_t index)
{
    std::vector<std::filesystem::path> list = rv_editor_recent_load();
    std::erase(list, root);
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index, list.size())), root);
    if (list.size() > rv_editor_recent_max) {
        list.resize(rv_editor_recent_max);
    }
    rv_editor_recent_save(list);
}

} // namespace rv_editor
