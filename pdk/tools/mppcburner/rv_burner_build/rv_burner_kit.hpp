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

// --- the Steam Runtime 4 reference ---
//
// The reference is R/tools/linux-compat-check.cmake and R/SteamLinuxRuntime_4, where R is the parent of the
// burner's directory. The compat check proves a disc.so's needs are in that Platform. R/tools/steamrt4-toolchain.cmake
// is shipped by the development kit delivery. It sets the delivery's Steam Runtime 4 SDK as the sysroot, so the
// headers and libraries a disc.so links against come from that SDK. Without it no sysroot is set, so
// headers and libraries come from the compiler's default search paths. The compiler comes from PATH in both cases.

/// Find the Steam Runtime 4 reference beside the running burner, as the build and the delivery lay it out.
///
/// @param toolchain_file  receives R/tools/steamrt4-toolchain.cmake when it exists; left empty when it does not
/// @param compat_check    receives R/tools/linux-compat-check.cmake on success
/// @param runtime_dir     receives R/SteamLinuxRuntime_4 on success
/// @param error           set with the reason when the reference is missing
/// @return RV_OK with the paths; RV_ERR_NOENT when the burner's directory is unknown or the compat check or the
///         runtime directory is absent, with any filesystem error that hid it appended to the reason;
///         RV_ERR_IO when the toolchain file cannot be inspected
int rv_burner_linux_kit_locate(std::filesystem::path &toolchain_file,
    std::filesystem::path &compat_check,
    std::filesystem::path &runtime_dir,
    std::string &error);

/// Run the compat check on one file against the runtime's Platform:
/// cmake -DRV_COMPAT_FILES=<file> -DRV_COMPAT_RUNTIME=<runtime_dir> -P <compat_check>.
///
/// On failure the check's own output is printed, since it names the symbol or library that is too new.
///
/// @param compat_check  R/tools/linux-compat-check.cmake from rv_burner_linux_kit_locate()
/// @param runtime_dir   R/SteamLinuxRuntime_4 from rv_burner_linux_kit_locate()
/// @param file          the binary to check, e.g. the disc's disc.so
/// @param error         set naming the file when it does not meet the contract
/// @return RV_OK when the check exits 0; RV_ERR_INVAL when the check reports a failure (exit 1) or a path holds
///         ';'; RV_ERR_IO when cmake cannot run the check: not started, not found, or killed
int rv_burner_linux_compat_check(const std::filesystem::path &compat_check,
    const std::filesystem::path &runtime_dir,
    const std::filesystem::path &file,
    std::string &error);

} // namespace rv_pdktools
