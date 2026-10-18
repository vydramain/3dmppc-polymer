#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rv_pdktools
{

// --- the web kit ---
//
// The kit is the browser firmware a disc is linked into: the console's static
// libraries and kit.txt, written by the build next to them. The burner finds it
// beside its own binary, so a tools package carries it without any setting.

/// What kit.txt says, with every path already resolved against the kit directory.
struct rv_burner_kit {
    /// The kit directory.
    std::filesystem::path dir;
    /// PDK version the console was built against.
    int pdk_major = 0;
    int pdk_minor = 0;
    /// CMAKE_BUILD_TYPE of the console.
    std::string build_type;
    /// Static libraries, in link order.
    std::vector<std::filesystem::path> libs;
    /// libs and the disc archive go in one --start-group.
    bool link_group = false;
    /// emcc flags of the console's link line.
    std::vector<std::string> link_flags;
    /// The --pre-js file.
    std::filesystem::path pre_js;
    /// The HTML page shell of the disc's index.html.
    std::filesystem::path shell;
    /// License files of the third-party parts the kit ships.
    std::vector<std::filesystem::path> licenses;
};

/// Find the kit: firmware/web beside the running burner, as the build installs it.
///
/// @param dir    receives the kit directory on success
/// @param error  set with the reason when there is no kit
/// @return RV_OK, or RV_ERR_NOENT when the burner's directory is unknown or kit.txt is missing
int rv_burner_kit_locate(std::filesystem::path &dir, std::string &error);

/// Read and check kit.txt in @p dir.
///
/// The PDK version is checked first, so a kit of another PDK is named as a version mismatch even
/// when its keys differ. Then every key must be present exactly once and none may be unknown, and
/// every file the kit names must exist inside the kit directory.
///
/// @param dir    the kit directory from rv_burner_kit_locate()
/// @param out    receives the parsed kit; unspecified on failure
/// @param error  set naming the key, line, file or version that is wrong
/// @return RV_OK; RV_ERR_INVAL on a malformed or incomplete kit.txt or a kit of another PDK;
///         RV_ERR_NOENT on a missing kit file; RV_ERR_IO when kit.txt cannot be read
int rv_burner_kit_read(const std::filesystem::path &dir, rv_burner_kit &out, std::string &error);

/// Refuse a kit built against another PDK. The match is exact: the disc is compiled against
/// this burner's headers and linked with the console's libraries, so both must share one ABI.
///
/// @param kit    a kit from rv_burner_kit_read()
/// @param error  set with both versions when they differ
/// @return RV_OK when major and minor equal RV_MPPC_VER_MAJOR and RV_MPPC_VER_MINOR, RV_ERR_INVAL otherwise
int rv_burner_kit_check_version(const rv_burner_kit &kit, std::string &error);

// --- the Linux kit ---
//
// The Linux kit is firmware/linux beside the burner: the sysroot toolchain file a disc's native
// code is configured with, and the compat check that proves the result runs on the delivery target.
// A build tree without it is built against this machine's libraries, so it is optional.

/// Find the Linux kit: firmware/linux beside the running burner, as the development build installs it.
///
/// @param toolchain_file  receives firmware/linux/toolchain.cmake on success
/// @param compat_check    receives firmware/linux/compat-check.cmake on success
/// @param error           set with the reason when there is no kit or it is incomplete
/// @return RV_OK with both paths; RV_ERR_NOENT when the directory is absent or the burner's directory
///         is unknown; RV_ERR_INVAL when the directory exists but one of its two files does not
int rv_burner_linux_kit_locate(std::filesystem::path &toolchain_file, std::filesystem::path &compat_check, std::string &error);

/// Run the compat check on one file: cmake -DRV_COMPAT_FILES=<file> -P <compat_check>.
///
/// On failure the check's own output is printed, since it names the symbol or library that is too new.
///
/// @param compat_check  the kit's compat-check.cmake
/// @param file          the binary to check, e.g. the disc's disc.so
/// @param error         set naming the file when it does not meet the contract
/// @return RV_OK when the check exits 0; RV_ERR_INVAL when it does not
int rv_burner_linux_compat_check(const std::filesystem::path &compat_check,
    const std::filesystem::path &file,
    std::string &error);

} // namespace rv_pdktools
