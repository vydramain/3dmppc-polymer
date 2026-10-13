#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "rv_burner_common/rv_burner_process.hpp"
#include "rv_burner_compile/rv_burner_cmake.hpp"
#include "rv_burner_compile/rv_burner_compile_sources.hpp"
#include "rv_burner_zip/rv_burner_zipwrite.hpp"
#include "rv_burner_kit.hpp"
#include "rv_burner_setup/rv_burner_setup.hpp"
#include "rv_burner_target.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The web target: the disc is compiled to a static archive and linked with the kit into one page.
static constexpr const char *k_disc_extension = ".mppcdisc";                // the intermediate image the page embeds
static constexpr const char *k_web_project_subdir = "web";                  // the generated disc project, in the build tree
static constexpr const char *k_web_binary_subdir = "build";                 // its cmake binary directory
static constexpr const char *k_emsdk_compiler = "upstream/emscripten/em++"; // the em++ driver (emcc for C++) in the emsdk
static constexpr const char *k_emsdk_toolchain = "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake";
static constexpr const char *k_page_name = "index.html";          // the single-file page, beside the image
static constexpr const char *k_opt_level = "-O3";                 // speed: the page runs in a browser; built without -g
static constexpr const char *k_group_begin = "-Wl,--start-group"; // libs and archive resolve each other in any order
static constexpr const char *k_group_end = "-Wl,--end-group";
static constexpr const char *k_pre_js_flag = "--pre-js";
static constexpr const char *k_shell_flag = "--shell-file";     // the page shell the kit names
static constexpr const char *k_single_file = "-sSINGLE_FILE=1"; // one html carries the code and the disc
static constexpr const char *k_embed_flag = "--embed-file";
static constexpr const char *k_embed_suffix = "@/disc.mppcdisc";      // the disc's name in the page's file system
static constexpr const char *k_no_growth = "-sALLOW_MEMORY_GROWTH=0"; // the heap never grows past its start
static constexpr const char *k_math_lib = "-lm";
static constexpr const char *k_output_flag = "-o";
static constexpr const char *k_licenses_subdir = "licenses"; // the kit's license files, beside index.html
static constexpr const char *k_zip_extension = ".zip";       // DIR.zip beside DIR: the page and its licenses in one file
// 256 MiB: a fixed budget several times the console's measured needs (about 22 MB, 15 MB and 3 MB, plus the disc).
// The console sizes its RAM from the heap maximum, so k_no_growth keeps that maximum equal to this start.
static constexpr std::size_t k_initial_memory = std::size_t{ 256 } * 1024 * 1024;

// The emsdk directory: --emsdk when given, else the one setup web installs.
static int emsdk_directory(const rv_burner_options &options, fs::path &dir, std::string &error)
{
    if (!options.emsdk.empty()) {
        dir = options.emsdk;
        return RV_OK;
    }
    return rv_burner_emscripten_dir(dir, error);
}

// The kit and the emsdk the page is built from. Checked before any compile, and again at finish.
static int locate_web_tools(const rv_burner_options &options, rv_burner_kit &kit, fs::path &emsdk, std::string &error)
{
    fs::path kit_dir;
    const int locate_err = rv_burner_kit_locate(kit_dir, error);
    if (RV_OK != locate_err) {
        return locate_err;
    }
    const int read_err = rv_burner_kit_read(kit_dir, kit, error);
    if (RV_OK != read_err) {
        return read_err;
    }
    const int version_err = rv_burner_kit_check_version(kit, error);
    if (RV_OK != version_err) {
        return version_err;
    }
    const int dir_err = emsdk_directory(options, emsdk, error);
    if (RV_OK != dir_err) {
        return dir_err;
    }
    const fs::path compiler = emsdk / k_emsdk_compiler;
    std::error_code ec;
    const bool found = fs::is_regular_file(compiler, ec);
    if (ec) {
        error = std::format("cannot check '{}': {}", compiler.string(), ec.message());
        return RV_ERR_IO;
    }
    if (!found) {
        error = std::format("no emscripten at '{}': run `mppcburner setup web` or pass --emsdk", emsdk.string());
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// web: -o names a directory that receives index.html; the disc image sits beside it.
int resolve_web(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &manifest,
    rv_burner_destination &destination,
    std::string &error)
{
    if (!options.unpacked.empty()) {
        error = "--target web writes a directory: give it -o PATH, not -u";
        return RV_ERR_INVAL;
    }
    if (options.output.empty()) {
        error = "--target web needs -o PATH, the directory to write";
        return RV_ERR_INVAL;
    }
    // The web console is built without LuaJIT, so it has no Lua machine for a script disc to run in.
    const bool has_scripts = !manifest.scripts_sources.empty() || manifest.budget.pccl.script_memory_size > 0;
    if (has_scripts) {
        error = "--target web cannot take a disc with scripts: the web console has no Lua machine";
        return RV_ERR_INVAL;
    }
    rv_burner_kit kit;
    fs::path emsdk;
    const int tools_err = locate_web_tools(options, kit, emsdk, error);
    if (RV_OK != tools_err) {
        return tools_err;
    }
    std::error_code ec;
    const fs::path dir = fs::absolute(fs::path(options.output), ec);
    if (!ec) {
        fs::create_directories(dir, ec);
    }
    if (ec) {
        error = std::format("cannot create '{}': {}", options.output, ec.message());
        return RV_ERR_IO;
    }
    destination.kind = rv_burner_destination_kind::archive;
    destination.path = dir / (manifest.disc_id + k_disc_extension);
    return RV_OK;
}

static fs::path web_project_dir(const rv_burner_finish_input &input)
{
    return input.project_dir / k_web_project_subdir;
}

static fs::path web_binary_dir(const rv_burner_finish_input &input)
{
    return web_project_dir(input) / k_web_binary_subdir;
}

// Generates the disc project as a web static archive, configures it with the Emscripten toolchain, and builds it.
static int
compile_web_disc(const rv_burner_finish_input &input, const rv_burner_kit &kit, const fs::path &emsdk, std::string &error)
{
    const fs::path project_dir = web_project_dir(input);
    const fs::path binary_dir = web_binary_dir(input);
    std::error_code ec;
    fs::create_directories(binary_dir, ec);
    if (ec) {
        error = std::format("cannot create '{}': {}", binary_dir.string(), ec.message());
        return RV_ERR_IO;
    }
    const int cmake_err = create_cmakelists(input.options,
        input.manifest,
        project_dir,
        input.absolute_includes,
        input.absolute_sources,
        error,
        rv_disc_project_kind::web_static);
    if (RV_OK != cmake_err) {
        return cmake_err;
    }
    const fs::path toolchain = emsdk / k_emsdk_toolchain;
    const int configure_err = configure_cmake(binary_dir, project_dir, error, toolchain, kit.build_type);
    if (RV_OK != configure_err) {
        return configure_err;
    }

    std::string child_output;
    const int status = build_cmake_project(binary_dir, input.options.jobs, child_output);
    if (status != 0) {
        dump_child_output(child_output);
        error = std::format("compiling the web disc failed (exit {})", status);
        return RV_ERR_IO;
    }

    const fs::path archive = binary_dir / disc_artifact_name(rv_disc_project_kind::web_static);
    if (!fs::is_regular_file(archive, ec)) {
        dump_child_output(child_output);
        error = "the build reported success but produced no '" + archive.string() + "'";
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// --embed-file SRC@DST splits at the first '@', so a literal '@' in SRC is '@@' (file_packager.py).
static std::string escape_embed_source(const std::string &path)
{
    std::string escaped;
    for (const char c : path) {
        escaped += c;
        if (c == '@') {
            escaped += c;
        }
    }
    return escaped;
}

// Words joined by single spaces: one shell line.
static std::string join_words(const std::vector<std::string> &words)
{
    std::string line;
    for (const std::string &word : words) {
        line += line.empty() ? word : " " + word;
    }
    return line;
}

// Links the kit's libraries and the disc archive with the page shell into index.html beside the image.
static int
link_web_page(const rv_burner_finish_input &input, const rv_burner_kit &kit, const fs::path &emsdk, std::string &error)
{
    const fs::path archive = web_binary_dir(input) / disc_artifact_name(rv_disc_project_kind::web_static);
    const fs::path page_dir = input.destination.path.parent_path();
    const fs::path page = page_dir / k_page_name;
    std::vector<std::string> words{ shell_quote((emsdk / k_emsdk_compiler).string()), k_opt_level };
    if (kit.link_group) {
        words.push_back(k_group_begin);
    }
    for (const fs::path &lib : kit.libs) {
        words.push_back(shell_quote(lib.string()));
    }
    words.push_back(shell_quote(archive.string()));
    if (kit.link_group) {
        words.push_back(k_group_end);
    }
    for (const std::string &flag : kit.link_flags) {
        words.push_back(shell_quote(flag));
    }
    words.push_back(k_pre_js_flag);
    words.push_back(shell_quote(kit.pre_js.string()));
    words.push_back(k_shell_flag);
    words.push_back(shell_quote(kit.shell.string()));
    words.push_back(k_single_file);
    words.push_back(k_embed_flag);
    words.push_back(shell_quote(escape_embed_source(input.destination.path.string()) + k_embed_suffix));
    words.push_back(std::format("-sINITIAL_MEMORY={}", k_initial_memory));
    words.push_back(k_no_growth);
    words.push_back(k_math_lib);
    words.push_back(k_output_flag);
    words.push_back(shell_quote(page.string()));
    const std::string link = join_words(words);

    std::string child_output;
    const int status = run_capture(link, child_output);
    if (status != 0) {
        dump_child_output(child_output);
        error = std::format("linking the web page failed (exit {})", status);
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Copies each license the kit names into DIR/licenses/, under its own file name.
static int copy_licenses(const rv_burner_kit &kit, const fs::path &page_dir, std::string &error)
{
    const fs::path dir = page_dir / k_licenses_subdir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = std::format("cannot create '{}': {}", dir.string(), ec.message());
        return RV_ERR_IO;
    }
    for (const fs::path &license : kit.licenses) {
        const fs::path dest = dir / license.filename();
        fs::copy_file(license, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = std::format("cannot copy license '{}' to '{}': {}", license.string(), dest.string(), ec.message());
            return RV_ERR_IO;
        }
    }
    return RV_OK;
}

// The image is embedded in the page, so it is not kept beside it. A missing image is not an error.
static int remove_image(const fs::path &image, std::string &error)
{
    std::error_code ec;
    fs::remove(image, ec);
    if (ec) {
        error = std::format("cannot remove '{}': {}", image.string(), ec.message());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// DIR.zip beside DIR: the page and its licenses in one file.
static fs::path web_zip_path(const fs::path &page_dir)
{
    return page_dir.parent_path() / (page_dir.filename().string() + k_zip_extension);
}

// Adds index.html, then each license copied into licenses/.
static int add_page_entries(rv_zipwriter &zip, const fs::path &page_dir, const rv_burner_kit &kit, std::string &error)
{
    const int page_err = zip.add_file(k_page_name, (page_dir / k_page_name).string(), error);
    if (RV_OK != page_err) {
        return page_err;
    }
    for (const fs::path &license : kit.licenses) {
        const std::string name = std::string(k_licenses_subdir) + "/" + license.filename().string();
        const fs::path source = page_dir / k_licenses_subdir / license.filename();
        const int add_err = zip.add_file(name, source.string(), error);
        if (RV_OK != add_err) {
            return add_err;
        }
    }
    return RV_OK;
}

// Writes DIR.zip; an existing one is replaced by the writer's truncating open.
static int write_zip(const fs::path &page_dir, const rv_burner_kit &kit, std::string &error)
{
    const fs::path zip_path = web_zip_path(page_dir);
    rv_zipwriter zip(zip_path.string());
    if (!zip.ok()) {
        error = "cannot write '" + zip_path.string() + "'";
        return RV_ERR_IO;
    }
    const int entries_err = add_page_entries(zip, page_dir, kit, error);
    if (RV_OK != entries_err) {
        return entries_err;
    }
    const int finish_err = zip.finish(error);
    if (RV_OK != finish_err) {
        return finish_err;
    }
    return RV_OK;
}

// Compiles the disc as an archive, then links it into the page. Runs after the burn wrote the image.
int finish_web(const rv_burner_finish_input &input, std::string &error)
{
    rv_burner_kit kit;
    fs::path emsdk;
    const int tools_err = locate_web_tools(input.options, kit, emsdk, error);
    if (RV_OK != tools_err) {
        return tools_err;
    }
    const int compile_err = compile_web_disc(input, kit, emsdk, error);
    if (RV_OK != compile_err) {
        return compile_err;
    }
    const int link_err = link_web_page(input, kit, emsdk, error);
    if (RV_OK != link_err) {
        return link_err;
    }
    const fs::path page_dir = input.destination.path.parent_path();
    const int licenses_err = copy_licenses(kit, page_dir, error);
    if (RV_OK != licenses_err) {
        return licenses_err;
    }
    const int remove_err = remove_image(input.destination.path, error);
    if (RV_OK != remove_err) {
        return remove_err;
    }
    const int zip_err = write_zip(page_dir, kit, error);
    if (RV_OK != zip_err) {
        return zip_err;
    }
    const std::string dir_name = page_dir.string();
    const std::string zip_name = web_zip_path(page_dir).string();
    rv_pdklib::rv_fprintf(stderr,
        "web: %s (%s, %s/) and %s\n",
        dir_name.c_str(),
        k_page_name,
        k_licenses_subdir,
        zip_name.c_str());
    return RV_OK;
}

} // namespace rv_pdktools
