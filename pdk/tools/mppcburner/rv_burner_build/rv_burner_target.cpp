#include "rv_burner_target.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

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

// The player target: the console the discs run in, copied beside the image.
static constexpr const char *k_firmware_subdir = "firmware";
static constexpr const char *k_firmware_name = "3dmppc";
static constexpr const char *k_disc_extension = ".mppcdisc";
static constexpr fs::perms k_exec_bits = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;

// The player console the development build leaves beside mppcburner, at firmware/3dmppc.
static int locate_firmware(fs::path &firmware, std::string &error)
{
    const fs::path exe_dir = executable_directory();
    if (exe_dir.empty()) {
        error = "cannot find the directory of mppcburner, so no player console";
        return RV_ERR_NOENT;
    }
    firmware = exe_dir / k_firmware_subdir / k_firmware_name;
    std::error_code ec;
    if (!fs::is_regular_file(firmware, ec)) {
        error =
            std::format("no player console at '{}'; a development build (-D3DMPPC_DEVTOOLS=ON) makes it", firmware.string());
        return RV_ERR_NOENT;
    }
    return RV_OK;
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

// player: -o names a directory that receives <disc id>.mppcdisc and, in finish, the console.
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

// Copies the player console beside the image, executable, and says what landed in the directory.
static int finish_player(const rv_burner_destination &destination, std::string &error)
{
    fs::path firmware;
    const int firmware_err = locate_firmware(firmware, error);
    if (RV_OK != firmware_err) {
        return firmware_err;
    }
    const fs::path dir = destination.path.parent_path();
    const fs::path dest = dir / k_firmware_name;
    std::error_code ec;
    fs::copy_file(firmware, dest, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        fs::permissions(dest, k_exec_bits, fs::perm_options::add, ec);
    }
    if (ec) {
        error = std::format("cannot copy the player console to '{}': {}", dest.string(), ec.message());
        return RV_ERR_IO;
    }
    const std::string dir_name = dir.string();
    const std::string file_name = destination.path.filename().string();
    rv_pdklib::rv_fprintf(stderr, "player: %s (%s, %s)\n", dir_name.c_str(), k_firmware_name, file_name.c_str());
    return RV_OK;
}

constexpr rv_burner_target_spec TARGETS[] = {
    { k_target_disc, resolve_disc, nullptr },
    { k_target_player, resolve_player, finish_player },
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
