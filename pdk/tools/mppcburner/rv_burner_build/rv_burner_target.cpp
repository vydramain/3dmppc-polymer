#include "rv_burner_target.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "rv_burner_common/rv_burner_process.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// -t picks what the burn writes. The table below is the only place a target is
// declared: a new target is a row, never a branch in rv_burner_build_run.
static constexpr const char *k_target_disc = "disc";
static constexpr const char *k_target_player = "player";
static constexpr const char *k_target_web = "web";

// The player target: the 3dmppc player delivery, copied beside the image. A release runs its
// launch file through Steam Linux Runtime 4, about 700 MB per release (Steam Linux Runtime 4 is
// 694 MB unpacked), so the whole delivery is copied rather than linked.
static constexpr const char *k_firmware_subdir = "firmware";
static constexpr const char *k_firmware_name = "3dmppc";
static constexpr const char *k_bin_dir = "bin";
static constexpr const char *k_runtime_dir = "SteamLinuxRuntime_4";
// Present in every Steam Linux Runtime 4; one without it was not written by a burn.
static constexpr const char *k_runtime_marker = "run";
// The web kit serves -t web; a player release does not carry it.
static constexpr const char *k_web_dir = "web";
static constexpr const char *k_disc_extension = ".mppcdisc";
// Links inside Steam Linux Runtime 4 stay links; overwrite replaces a previous release's files and leaves saves in bin/.
static constexpr fs::copy_options k_copy_options =
    fs::copy_options::copy_symlinks | fs::copy_options::recursive | fs::copy_options::overwrite_existing;

// A file the player delivery must hold. A missing one names the path and how a development build makes it.
static int check_delivery_file(const fs::path &path, std::string &error)
{
    std::error_code ec;
    const bool regular = fs::is_regular_file(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = std::format("cannot check '{}': {}", path.string(), ec.message());
        return RV_ERR_IO;
    }
    if (!regular) {
        error =
            std::format("no '{}' in the player delivery; a development build (-D3DMPPC_DEVTOOLS=ON) makes it", path.string());
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// The player delivery the development build leaves beside mppcburner, at firmware/.
static int locate_firmware(fs::path &firmware, std::string &error)
{
    const fs::path exe_dir = executable_directory();
    if (exe_dir.empty()) {
        error = "cannot find the directory of mppcburner, so no player delivery";
        return RV_ERR_NOENT;
    }
    firmware = exe_dir / k_firmware_subdir;
    const int binary_err = check_delivery_file(firmware / k_bin_dir / k_firmware_name, error);
    if (RV_OK != binary_err) {
        return binary_err;
    }
    return check_delivery_file(firmware / k_runtime_dir / k_runtime_marker, error);
}

// disc: one .mppcdisc at -o, or an unpacked directory at -u, exactly one of them.
static int resolve_disc(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &,
    rv_burner_destination &destination,
    std::string &error)
{
    // --unpacked and -o pick the same slot in the pipeline (where the burned result
    // goes) but produce different mediums, so exactly one of them names the output.
    if (options.output.empty() == options.unpacked.empty()) {
        error = "build needs exactly one of -o/--output or -u/--unpacked";
        return RV_ERR_INVAL;
    }
    const bool unpacked = !options.unpacked.empty();
    const std::string &operand = unpacked ? options.unpacked : options.output;
    std::error_code ec;
    destination.kind = unpacked ? rv_burner_destination_kind::directory : rv_burner_destination_kind::archive;
    destination.path = fs::absolute(fs::path(operand), ec);
    if (ec) {
        error = std::format("cannot resolve output path '{}'", operand);
        return RV_ERR_IO;
    }
    return RV_OK;
}

// weakly_canonical: the release directory may not exist yet, so only its existing prefix resolves.
static int resolve_canonical(const fs::path &path, fs::path &canonical, std::string &error)
{
    std::error_code ec;
    canonical = fs::weakly_canonical(path, ec);
    if (ec) {
        error = std::format("cannot resolve '{}': {}", path.string(), ec.message());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// True when path is base or lies under it; both are canonical.
static bool is_under(const fs::path &path, const fs::path &base)
{
    const fs::path relative = path.lexically_relative(base);
    return !relative.empty() && *relative.begin() != fs::path("..");
}

// A release writes DIR/SteamLinuxRuntime_4 and may remove it, so DIR must not be the delivery
// or inside it, nor the burner's directory or one of its ancestors: the burner runs in its own Steam Linux Runtime 4.
static int check_release_dir(const fs::path &dir, const fs::path &firmware, std::string &error)
{
    fs::path target;
    fs::path firmware_canonical;
    fs::path burner_canonical;
    const int target_err = resolve_canonical(dir, target, error);
    if (RV_OK != target_err) {
        return target_err;
    }
    const int firmware_err = resolve_canonical(firmware, firmware_canonical, error);
    if (RV_OK != firmware_err) {
        return firmware_err;
    }
    const int burner_err = resolve_canonical(firmware.parent_path(), burner_canonical, error);
    if (RV_OK != burner_err) {
        return burner_err;
    }
    if (is_under(target, firmware_canonical)) {
        error = std::format("'{}' is the player delivery or inside it; pick another -o", dir.string());
        return RV_ERR_INVAL;
    }
    if (is_under(burner_canonical, target)) {
        error = std::format("'{}' is the burner's directory or holds it; pick another -o", dir.string());
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

static const char *kind_name(bool directory)
{
    return directory ? "a directory" : "a file";
}

// The delivery's entries except web/, in the order the directory lists them.
static int list_delivery(const fs::path &firmware, std::vector<fs::directory_entry> &entries, std::string &error)
{
    std::error_code ec;
    fs::directory_iterator it(firmware, ec);
    if (ec) {
        error = std::format("cannot list '{}': {}", firmware.string(), ec.message());
        return RV_ERR_IO;
    }
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (it->path().filename() != k_web_dir) {
            entries.push_back(*it);
        }
    }
    // A failed increment leaves the iterator at end, so the loop ends here with ec set.
    if (ec) {
        error = std::format("cannot list '{}': {}", firmware.string(), ec.message());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// One path the copy writes, and everything under it. symlink_status, not status: the copy writes
// through a symbolic link in DIR, outside DIR, and a dangling link must not read as absent.
static int check_copy_tree(const fs::path &source, const fs::path &target, std::string &error)
{
    std::error_code ec;
    const fs::file_status existing = fs::symlink_status(target, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = std::format("cannot check '{}': {}", target.string(), ec.message());
        return RV_ERR_IO;
    }
    if (!fs::exists(existing)) {
        return RV_OK;
    }
    if (fs::is_symlink(existing)) {
        error = std::format("'{}' is a symbolic link; the release would write through it; remove it or pick another -o",
            target.string());
        return RV_ERR_INVAL;
    }
    const fs::file_status source_status = fs::symlink_status(source, ec);
    if (ec) {
        error = std::format("cannot check '{}': {}", source.string(), ec.message());
        return RV_ERR_IO;
    }
    if (fs::is_directory(existing) != fs::is_directory(source_status)) {
        error = std::format("'{}' is {} but the player delivery needs {}; move it or pick another -o",
            target.string(),
            kind_name(fs::is_directory(existing)),
            kind_name(fs::is_directory(source_status)));
        return RV_ERR_INVAL;
    }
    if (!fs::is_directory(existing)) {
        return RV_OK;
    }
    fs::directory_iterator it(source, ec);
    if (ec) {
        error = std::format("cannot list '{}': {}", source.string(), ec.message());
        return RV_ERR_IO;
    }
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        const int child_err = check_copy_tree(it->path(), target / it->path().filename(), error);
        if (RV_OK != child_err) {
            return child_err;
        }
    }
    if (ec) {
        error = std::format("cannot list '{}': {}", source.string(), ec.message());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// DIR/SteamLinuxRuntime_4 is replaced whole, so it is absent or a real directory with a regular `run`
// that a previous burn wrote. Anything else is refused, never deleted.
static int check_old_runtime(const fs::path &runtime, std::string &error)
{
    std::error_code ec;
    const fs::file_status existing = fs::symlink_status(runtime, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = std::format("cannot check '{}': {}", runtime.string(), ec.message());
        return RV_ERR_IO;
    }
    if (!fs::exists(existing)) {
        return RV_OK;
    }
    if (!fs::is_directory(existing)) {
        error = std::format("'{}' is not a Steam Linux Runtime 4 a burn wrote; remove it or pick another -o", runtime.string());
        return RV_ERR_INVAL;
    }
    const fs::path marker = runtime / k_runtime_marker;
    const fs::file_status run = fs::symlink_status(marker, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = std::format("cannot check '{}': {}", marker.string(), ec.message());
        return RV_ERR_IO;
    }
    if (!fs::is_regular_file(run)) {
        error = std::format(
            "'{}' is not a Steam Linux Runtime 4 a burn wrote (no regular '{}' in it); remove it or pick another -o",
            runtime.string(),
            k_runtime_marker);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Everything the copy will write is checked before the burn, so a refusal writes no .mppcdisc.
// SteamLinuxRuntime_4/ is checked as a whole; every other entry is walked, so nested paths under bin/ are covered too.
static int check_release_entries(const fs::path &firmware, const fs::path &dir, std::string &error)
{
    std::vector<fs::directory_entry> entries;
    const int list_err = list_delivery(firmware, entries, error);
    if (RV_OK != list_err) {
        return list_err;
    }
    for (const fs::directory_entry &entry : entries) {
        const fs::path target = dir / entry.path().filename();
        const int entry_err = entry.path().filename() == k_runtime_dir ? check_old_runtime(target, error) :
                                                                         check_copy_tree(entry.path(), target, error);
        if (RV_OK != entry_err) {
            return entry_err;
        }
    }
    return RV_OK;
}

// player: -o names a directory that receives <disc id>.mppcdisc and, in finish, the delivery.
static int resolve_player(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &manifest,
    rv_burner_destination &destination,
    std::string &error)
{
    if (!options.unpacked.empty()) {
        error = "--target player writes a directory: give it -o PATH, not -u";
        return RV_ERR_INVAL;
    }
    if (options.output.empty()) {
        error = "--target player needs -o PATH, the directory to write";
        return RV_ERR_INVAL;
    }
    fs::path firmware;
    const int firmware_err = locate_firmware(firmware, error);
    if (RV_OK != firmware_err) {
        return firmware_err;
    }
    std::error_code ec;
    const fs::path dir = fs::absolute(fs::path(options.output), ec);
    if (ec) {
        error = std::format("cannot resolve '{}': {}", options.output, ec.message());
        return RV_ERR_IO;
    }
    const int release_err = check_release_dir(dir, firmware, error);
    if (RV_OK != release_err) {
        return release_err;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        error = std::format("cannot create '{}': {}", options.output, ec.message());
        return RV_ERR_IO;
    }
    // Before the burn: a refusal here must not leave a finished .mppcdisc behind.
    const int entries_err = check_release_entries(firmware, dir, error);
    if (RV_OK != entries_err) {
        return entries_err;
    }
    destination.kind = rv_burner_destination_kind::archive;
    destination.path = dir / (manifest.disc_id + k_disc_extension);
    return RV_OK;
}

static int copy_entries(const std::vector<fs::directory_entry> &entries, const fs::path &dir, std::string &error)
{
    for (const fs::directory_entry &entry : entries) {
        const fs::path target = dir / entry.path().filename();
        std::error_code ec;
        fs::copy(entry.path(), target, k_copy_options, ec);
        if (ec) {
            error = std::format("cannot copy '{}' to '{}': {}", entry.path().string(), target.string(), ec.message());
            return RV_ERR_IO;
        }
    }
    return RV_OK;
}

// Replaces the earlier burn's Steam Linux Runtime 4, copies the delivery beside the image and says what landed.
// resolve_player checked every target, so only a filesystem failure stops here; then the release is partial
// and a re-burn into it restores it. bin/ is merged, not replaced, so the player's saves in DIR/bin survive.
static int finish_player(const rv_burner_finish_input &input, std::string &error)
{
    fs::path firmware;
    const int firmware_err = locate_firmware(firmware, error);
    if (RV_OK != firmware_err) {
        return firmware_err;
    }
    const fs::path dir = input.destination.path.parent_path();
    std::vector<fs::directory_entry> entries;
    const int list_err = list_delivery(firmware, entries, error);
    if (RV_OK != list_err) {
        return list_err;
    }
    std::error_code ec;
    fs::remove_all(dir / k_runtime_dir, ec);
    if (ec) {
        error = std::format("cannot remove the old Steam Linux Runtime 4 in '{}': {}", dir.string(), ec.message());
        return RV_ERR_IO;
    }
    const int copy_err = copy_entries(entries, dir, error);
    if (RV_OK != copy_err) {
        return copy_err;
    }
    const std::string dir_name = dir.string();
    const std::string file_name = input.destination.path.filename().string();
    rv_pdklib::rv_fprintf(stderr, "player: %s (%s, %s)\n", dir_name.c_str(), k_firmware_name, file_name.c_str());
    return RV_OK;
}

constexpr rv_burner_target_spec TARGETS[] = {
    { k_target_disc, resolve_disc, nullptr, true },
    { k_target_player, resolve_player, finish_player, true },
    { k_target_web, resolve_web, finish_web, false },
};

const rv_burner_target_spec *find_target(std::string_view name)
{
    const std::string_view wanted = name.empty() ? std::string_view(k_target_disc) : name;
    for (const rv_burner_target_spec &spec : TARGETS) {
        if (wanted == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

std::string target_names()
{
    std::string names;
    for (const rv_burner_target_spec &spec : TARGETS) {
        names += names.empty() ? spec.name : std::string(", ") + spec.name;
    }
    return names;
}

} // namespace rv_pdktools
