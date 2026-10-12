#include "rv_burner_setup.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

#include "pdk/rv_err.h"

#include "rv_burner_common/rv_burner_process.hpp"
#include "rv_burner_print.hpp"

namespace rv_pdktools
{
namespace
{
// Pinned emsdk release. Changing it changes the compiler every web disc is built with.
constexpr const char *k_emsdk_version = "6.0.10";
// GitHub archive of the release tag; its top directory emsdk-<version>/ is stripped on unpack.
constexpr const char *k_emsdk_url_prefix = "https://github.com/emscripten-core/emsdk/archive/refs/tags/";
constexpr const char *k_emsdk_url_suffix = ".tar.gz";
constexpr const char *k_tar_strip_flag = "--strip-components=1";
// sha256 of that archive. Checked before anything in it is unpacked or run.
constexpr const char *k_emsdk_sha256 = "09cbafdf00e5a7b4275fc27229d69befafc5ca6070543124219977101310dade";
// Hex digits in a sha256 digest, the first field sha256sum prints.
constexpr std::size_t k_sha256_hex_chars = 64;
// sha256sum puts a backslash before the digest when the file name has a backslash or a newline in it.
constexpr char k_sha256_escape = '\\';

// Install root: $XDG_DATA_HOME, or $HOME/.local/share when that is unset or empty.
constexpr const char *k_xdg_data_home_var = "XDG_DATA_HOME";
constexpr const char *k_home_var = "HOME";
constexpr const char *k_home_data_subdir = ".local/share";
// Our own directory under the data root, shared by every 3dmppc tool that installs something.
constexpr const char *k_vendor_dir = "3dmppc";
constexpr const char *k_emscripten_dir_prefix = "emscripten-";
// Written as the last step. Its presence is the only sign that an install completed.
constexpr const char *k_ready_marker = ".mppcburner-ready";
// The download sits next to the install directory while it is fetched and checked.
constexpr const char *k_download_suffix = ".download";

// emsdk's own scripts, run from inside the emsdk directory.
constexpr const char *k_emsdk_script = "./emsdk";
constexpr const char *k_embuilder_script = "upstream/emscripten/embuilder";
// Prebuilds the libc and libc++ variants for wasm so the first web burn does not wait for them.
constexpr const char *k_embuilder_target = "MINIMAL";

// Steps printed as [n/N]: check, download, verify, unpack, install, embuilder, mark.
constexpr int k_setup_steps = 7;

// Runs one child step. On failure the child's own words go out first, then the reason.
int run_child(const std::string &command, const char *what, std::string &output)
{
    const int status = run_capture(command, output);
    if (0 == status) {
        return RV_OK;
    }
    if (-1 == status) {
        rv_burner_print_error(std::string("could not start a shell for ") + what);
        return RV_ERR_IO;
    }
    dump_child_output(output);
    rv_burner_print_error(std::string(what) + " failed with status " + std::to_string(status));
    return RV_ERR_IO;
}

// Removes the download when install_web returns, on every return path, so a failed or half-fetched
// archive does not stay behind. A run killed by a signal skips this; the next run's curl -o overwrites it.
struct download_guard {
    std::filesystem::path path;

    ~download_guard()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

int download_archive(const std::string &url, const std::filesystem::path &target)
{
    std::string output;
    const std::string command = "curl -fsSL -o " + shell_quote(target.string()) + " " + shell_quote(url);
    return run_child(command, "download", output);
}

int verify_archive(const std::filesystem::path &archive)
{
    std::string output;
    const int rc = run_child("sha256sum " + shell_quote(archive.string()), "sha256sum", output);
    if (RV_OK != rc) {
        return rc;
    }

    const std::size_t digest_start = (!output.empty() && k_sha256_escape == output[0]) ? 1 : 0;
    const std::string digest = output.substr(digest_start, k_sha256_hex_chars);
    if (digest != k_emsdk_sha256) {
        rv_burner_print_error("sha256 of " + archive.string() + " is " + digest + ", expected " + k_emsdk_sha256);
        return RV_ERR_IO;
    }
    return RV_OK;
}

int unpack_archive(const std::filesystem::path &archive, const std::filesystem::path &dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        rv_burner_print_error("cannot create " + dir.string() + ": " + ec.message());
        return RV_ERR_IO;
    }

    std::string output;
    const std::string command =
        "tar -xzf " + shell_quote(archive.string()) + " -C " + shell_quote(dir.string()) + " " + k_tar_strip_flag;
    return run_child(command, "unpack", output);
}

int install_emsdk(const std::filesystem::path &dir)
{
    const std::string enter = "cd " + shell_quote(dir.string()) + " && ";
    std::string output;
    const int rc = run_child(enter + k_emsdk_script + " install " + k_emsdk_version, "emsdk install", output);
    if (RV_OK != rc) {
        return rc;
    }
    return run_child(enter + k_emsdk_script + " activate " + k_emsdk_version, "emsdk activate", output);
}

int prebuild_libraries(const std::filesystem::path &dir)
{
    std::string output;
    const std::string command =
        "cd " + shell_quote(dir.string()) + " && " + k_embuilder_script + " build " + k_embuilder_target;
    return run_child(command, "embuilder", output);
}

int write_ready_marker(const std::filesystem::path &dir)
{
    const std::filesystem::path marker = dir / k_ready_marker;
    std::ofstream out(marker);
    out << k_emsdk_version << '\n';
    out.close();
    if (out.fail()) {
        rv_burner_print_error("cannot write " + marker.string());
        return RV_ERR_IO;
    }
    return RV_OK;
}

int install_web()
{
    std::filesystem::path dir;
    std::string reason;
    const int dir_rc = rv_burner_emscripten_dir(dir, reason);
    if (RV_OK != dir_rc) {
        rv_burner_print_error(reason);
        return dir_rc;
    }

    // --- [1/7] check ---
    std::error_code ec;
    const bool installed = std::filesystem::exists(dir / k_ready_marker, ec);
    if (ec) {
        rv_burner_print_error("cannot look into " + dir.string() + ": " + ec.message());
        return RV_ERR_IO;
    }
    if (installed) {
        rv_burner_print_step(1, "check", "already installed at " + dir.string(), k_setup_steps);
        return RV_OK;
    }
    rv_burner_print_step(1, "check", "not installed; installing into " + dir.string(), k_setup_steps);

    std::filesystem::create_directories(dir.parent_path(), ec);
    if (ec) {
        rv_burner_print_error("cannot create " + dir.parent_path().string() + ": " + ec.message());
        return RV_ERR_IO;
    }

    const std::string url = std::string(k_emsdk_url_prefix) + k_emsdk_version + k_emsdk_url_suffix;
    const download_guard download{ dir.string() + k_download_suffix };

    // --- [2/7] download ---
    rv_burner_print_step(2, "download", url, k_setup_steps);
    int rc = download_archive(url, download.path);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [3/7] verify ---
    rv_burner_print_step(3, "verify", "sha256 " + std::string(k_emsdk_sha256), k_setup_steps);
    rc = verify_archive(download.path);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [4/7] unpack ---
    rv_burner_print_step(4, "unpack", dir.string(), k_setup_steps);
    rc = unpack_archive(download.path, dir);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [5/7] install ---
    rv_burner_print_step(5, "install", "emsdk " + std::string(k_emsdk_version) + " install and activate", k_setup_steps);
    rc = install_emsdk(dir);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [6/7] embuilder ---
    rv_burner_print_step(6, "embuilder", std::string("prebuild ") + k_embuilder_target + " libraries", k_setup_steps);
    rc = prebuild_libraries(dir);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [7/7] mark ---
    rc = write_ready_marker(dir);
    if (RV_OK != rc) {
        return rc;
    }
    rv_burner_print_step(7, "mark", "installed at " + dir.string(), k_setup_steps);
    return RV_OK;
}

struct setup_tool {
    const char *name;
    int (*install)();
};

// The tools `setup` knows. A new tool is a row here, never a branch in rv_burner_setup_run.
constexpr setup_tool k_tools[] = {
    { "web", install_web },
};
} // namespace
} // namespace rv_pdktools

int rv_pdktools::rv_burner_setup_run(const rv_burner_options &options)
{
    for (const setup_tool &tool : k_tools) {
        if (options.operand == tool.name) {
            return tool.install();
        }
    }

    std::string known;
    for (const setup_tool &tool : k_tools) {
        known += (known.empty() ? "" : ", ") + std::string(tool.name);
    }
    rv_burner_print_error("unknown tool '" + options.operand + "'; known tools: " + known);
    return RV_ERR_INVAL;
}

int rv_pdktools::rv_burner_emscripten_dir(std::filesystem::path &dir, std::string &reason)
{
    const char *xdg = std::getenv(k_xdg_data_home_var);
    const char *home = std::getenv(k_home_var);
    const bool has_xdg = nullptr != xdg && '\0' != *xdg;
    const bool has_home = nullptr != home && '\0' != *home;
    if (!has_xdg && !has_home) {
        reason = "neither XDG_DATA_HOME nor HOME is set, so there is no place to install the web tools";
        return RV_ERR_NOENT;
    }

    const std::filesystem::path data_root =
        has_xdg ? std::filesystem::path(xdg) : std::filesystem::path(home) / k_home_data_subdir;
    dir = data_root / k_vendor_dir / (std::string(k_emscripten_dir_prefix) + k_emsdk_version);
    return RV_OK;
}
