#include "rv_burner_setup.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

// Detail prefix of the finished install, used by the check and the mark step.
constexpr const char *k_installed_at = "installed at ";
// Ends the detail of a step whose child output is captured and shown only on failure.
constexpr const char *k_silent_note = ". Nothing is printed until the step ends";

// emsdk.py's download bar is 80 - 4 = 76 wide: one dash per 1/76 of the archive, the only progress it prints.
constexpr int k_emsdk_bar_width = 76;
// emsdk.py lines: "Downloading: <name> from <url>[, <N> Bytes]", then " [", the dashes, "]".
constexpr std::string_view k_emsdk_header = "Downloading: ";
constexpr std::string_view k_emsdk_from = " from ";
constexpr std::string_view k_emsdk_bytes = " Bytes";
constexpr std::string_view k_emsdk_bar_open = " [";
constexpr char k_emsdk_dash = '-';
constexpr char k_emsdk_bar_close = ']';
// curl -# ends each update with the share, e.g. "45.3%".
constexpr char k_curl_percent = '%';
// A share line prints only when the integer share reaches the next multiple of this, so a terminal is not flooded.
constexpr int k_share_step = 5;
constexpr int k_share_full = 100;
// A header is the download path (at most PATH_MAX, 4096 bytes) plus emsdk's URL and size, far below this.
// A longer line is dropped so an unended one cannot grow the buffer.
constexpr std::size_t k_emsdk_line_max = 16384;

// One rung of the [n/N] ladder: its number and label, written once per step.
struct ladder_step {
    int number;
    const char *label;
};

// Where the emsdk parser is in its stream.
enum class emsdk_state {
    outside,     // any other line
    header_seen, // the last line was a header, so " [" comes next
    in_bar,      // inside " [ ... ]"
};

// Progress of one archive in one step. Output comes in pieces that may split lines anywhere, so the
// unfinished line waits in `line` between pieces.
struct progress_watch {
    progress_watch(ladder_step where, std::string archive)
        : step(where)
        , name(std::move(archive))
    {
    }

    ladder_step step;
    std::string name;
    std::string size; // human size; empty while the total is unknown
    std::string line;
    int rung = -1;      // share / k_share_step last printed, -1 before the first
    bool shown = false; // a share line was printed for this archive
    emsdk_state state = emsdk_state::outside;
    int dashes = 0;
};

void report_share(progress_watch &watch, int share)
{
    const int rung = share / k_share_step;
    if (rung <= watch.rung) {
        return;
    }
    watch.rung = rung;
    watch.shown = true;
    std::string detail = watch.name + ": " + std::to_string(share) + "%";
    if (!watch.size.empty()) {
        detail += " of " + watch.size;
    }
    rv_burner_print_step(watch.step.number, watch.step.label, detail, k_setup_steps);
}

void report_unknown_size(const progress_watch &watch)
{
    rv_burner_print_step(watch.step.number, watch.step.label, watch.name + ": size unknown", k_setup_steps);
}

bool is_digit_char(char c)
{
    return '0' <= c && c <= '9';
}

// Whole part of the number that ends @p text, "45.3" gives 45. False when no digit ends it.
bool trailing_share(const std::string &text, int &share)
{
    std::size_t begin = text.size();
    while (begin > 0 && (is_digit_char(text[begin - 1]) || '.' == text[begin - 1])) {
        --begin;
    }
    const std::size_t end = std::min(text.find('.', begin), text.size());
    if (begin == end) {
        return false;
    }
    return std::errc() == std::from_chars(text.data() + begin, text.data() + end, share).ec;
}

void feed_curl(progress_watch &watch, char c)
{
    if ('\r' == c || '\n' == c) {
        watch.line.clear();
        return;
    }
    if (k_curl_percent != c) {
        watch.line.push_back(c);
        return;
    }
    int share = 0;
    if (trailing_share(watch.line, share)) {
        report_share(watch, share);
    }
    watch.line.clear();
}

// The byte count of a header ending in ", <N> Bytes"; false when the header has none.
bool emsdk_header_size(const std::string &header, int64_t &bytes)
{
    if (!header.ends_with(k_emsdk_bytes)) {
        return false;
    }
    const std::size_t end = header.size() - k_emsdk_bytes.size();
    std::size_t begin = end;
    while (begin > 0 && is_digit_char(header[begin - 1])) {
        --begin;
    }
    if (begin == end) {
        return false;
    }
    return std::errc() == std::from_chars(header.data() + begin, header.data() + end, bytes).ec;
}

void begin_emsdk_archive(progress_watch &watch, const std::string &header)
{
    const std::size_t name_begin = k_emsdk_header.size();
    const std::size_t name_end = std::min(header.find(k_emsdk_from, name_begin), header.size());
    // emsdk prints the full destination path; the progress line names only the file.
    watch.name = std::filesystem::path(header.substr(name_begin, name_end - name_begin)).filename().string();
    watch.rung = -1;
    watch.dashes = 0;
    watch.size.clear();
    int64_t bytes = 0;
    if (!emsdk_header_size(header, bytes)) {
        report_unknown_size(watch);
        return;
    }
    watch.size = rv_burner_human_size(bytes);
}

void finish_emsdk_line(progress_watch &watch)
{
    // A CRLF child ends the header with a carriage return that must not reach the byte count.
    if (!watch.line.empty() && '\r' == watch.line.back()) {
        watch.line.pop_back();
    }
    const bool header = watch.line.starts_with(k_emsdk_header);
    watch.state = header ? emsdk_state::header_seen : emsdk_state::outside;
    if (header) {
        begin_emsdk_archive(watch, watch.line);
    }
    watch.line.clear();
}

void feed_emsdk_bar(progress_watch &watch, char c)
{
    if (k_emsdk_bar_close == c) {
        watch.state = emsdk_state::outside;
        return;
    }
    if (k_emsdk_dash != c) {
        return;
    }
    ++watch.dashes;
    report_share(watch, std::min(watch.dashes * k_share_full / k_emsdk_bar_width, k_share_full));
}

void feed_emsdk(progress_watch &watch, char c)
{
    if ('\n' == c) {
        finish_emsdk_line(watch);
        return;
    }
    if (emsdk_state::in_bar == watch.state) {
        feed_emsdk_bar(watch, c);
        return;
    }
    watch.line.push_back(c);
    if (k_emsdk_line_max < watch.line.size()) {
        watch.line.clear();
        watch.state = emsdk_state::outside;
        return;
    }
    if (emsdk_state::header_seen == watch.state && k_emsdk_bar_open == watch.line) {
        watch.state = emsdk_state::in_bar;
        watch.line.clear();
    }
}

// Runs one child step. On failure the child's own words go out first, then the reason.
// @p on_output sees each piece as the child writes it; an empty one prints nothing.
int run_child(const std::string &command, const char *what, std::string &output, const child_output_sink &on_output = {})
{
    const int status = run_capture_each(command, output, on_output);
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

int download_archive(const std::string &url, const std::filesystem::path &target, ladder_step step)
{
    progress_watch watch{ step, url.substr(url.rfind('/') + 1) };
    const child_output_sink on_output = [&watch](std::string_view piece) {
        for (const char c : piece) {
            feed_curl(watch, c);
        }
    };
    std::string output;
    const std::string command = "curl -fSL -# -o " + shell_quote(target.string()) + " " + shell_quote(url);
    const int rc = run_child(command, step.label, output, on_output);
    if (RV_OK != rc) {
        return rc;
    }
    if (!watch.shown) {
        report_unknown_size(watch);
    }
    return RV_OK;
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

int install_emsdk(const std::filesystem::path &dir, ladder_step step)
{
    progress_watch watch{ step, "" };
    const child_output_sink on_output = [&watch](std::string_view piece) {
        for (const char c : piece) {
            feed_emsdk(watch, c);
        }
    };
    const std::string enter = "cd " + shell_quote(dir.string()) + " && ";
    std::string output;
    const int rc = run_child(enter + k_emsdk_script + " install " + k_emsdk_version, "emsdk install", output, on_output);
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
        const std::string detail = std::string("already ") + k_installed_at + dir.string() + ", nothing is downloaded";
        rv_burner_print_step(1, "check", detail, k_setup_steps);
        return RV_OK;
    }
    const std::string check_detail = "not installed yet; installing once per user into " + dir.string();
    rv_burner_print_step(1, "check", check_detail, k_setup_steps);

    std::filesystem::create_directories(dir.parent_path(), ec);
    if (ec) {
        rv_burner_print_error("cannot create " + dir.parent_path().string() + ": " + ec.message());
        return RV_ERR_IO;
    }

    const std::string url = std::string(k_emsdk_url_prefix) + k_emsdk_version + k_emsdk_url_suffix;
    const download_guard download{ dir.string() + k_download_suffix };

    // --- [2/7] download ---
    constexpr ladder_step download_step{ 2, "download" };
    const std::string download_detail = url + ", the installer of the Emscripten compiler";
    rv_burner_print_step(download_step.number, download_step.label, download_detail, k_setup_steps);
    int rc = download_archive(url, download.path, download_step);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [3/7] verify ---
    const std::string verify_detail =
        "sha256 " + std::string(k_emsdk_sha256) + ", a changed or replaced archive is rejected before it is unpacked or run";
    rv_burner_print_step(3, "verify", verify_detail, k_setup_steps);
    rc = verify_archive(download.path);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [4/7] unpack ---
    const std::string unpack_detail = "emsdk scripts into " + dir.string();
    rv_burner_print_step(4, "unpack", unpack_detail, k_setup_steps);
    rc = unpack_archive(download.path, dir);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [5/7] install ---
    const std::string install_detail = "emsdk " + std::string(k_emsdk_version) +
        " downloads the Emscripten compiler (clang for wasm, Binaryen, Emscripten, Node.js) and makes it current;"
        " the install itself takes a long time";
    constexpr ladder_step install_step{ 5, "install" };
    rv_burner_print_step(install_step.number, install_step.label, install_detail, k_setup_steps);
    rc = install_emsdk(dir, install_step);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [6/7] embuilder ---
    const std::string embuilder_detail = std::string("prebuild ") + k_embuilder_target +
        " libraries: embuilder compiles libc and libc++ for wasm ahead of time, so the first web burn does not wait"
        " for them; the prebuild itself takes a long time" +
        k_silent_note;
    rv_burner_print_step(6, "embuilder", embuilder_detail, k_setup_steps);
    rc = prebuild_libraries(dir);
    if (RV_OK != rc) {
        return rc;
    }

    // --- [7/7] mark ---
    rc = write_ready_marker(dir);
    if (RV_OK != rc) {
        return rc;
    }
    const std::string mark_detail = std::string(k_installed_at) + dir.string() +
        ", written last so an interrupted install is never taken as complete; the next run skips the install";
    rv_burner_print_step(7, "mark", mark_detail, k_setup_steps);
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
