// Internals shared by rv_pcloader.cpp, rv_pcloader_mount.cpp,
// rv_pcloader_livedir_devtools.cpp and rv_pcloader_inspect.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/cd/rv_zipreader.hpp"

namespace rv_3dmppc
{

namespace rv_pcloader_detail
{

// The code entry's conventional name, and the ceiling on how much of it the
// console will allocate on the say-so of a disc: the bytes come out of a file
// that may have been downloaded from anywhere, so the size may not become a
// malloc argument unchecked. Both mount routes and bring_up() read these, so
// they live here rather than twice - two copies of a size ceiling is how the
// halves start disagreeing about it.
constexpr const char *RV_PCLOADER_DEFAULT_CODE_ENTRY = "disc.so";
constexpr int64_t RV_PCLOADER_CODE_MAX_SIZE = 128 << 20; // 128 MiB of code is already absurd

// Log escape length ceilings for errors during the load sequence.
// RV_PCLOADER_SYSTEM_ERROR_MAX_LEN: dlerror() and zip open error messages, which
//   come from the OS/library and can be verbose.
// RV_PCLOADER_MANIFEST_ERROR_MAX_LEN: manifest parse errors, raised before any disc
//   identity; room for a manifest parse error message.
constexpr std::size_t RV_PCLOADER_SYSTEM_ERROR_MAX_LEN = 160;
constexpr std::size_t RV_PCLOADER_MANIFEST_ERROR_MAX_LEN = 512;

// Which entry of the archive carries the code. The manifest names it; a
// manifest that leaves the key blank falls back to the conventional name.
// Both stages ask this, and they must agree: the version check reads the very
// entry the extraction later maps.
inline std::string code_entry_of(const rv_pdklib::rv_manifest &manifest)
{
    return manifest.budget.pccd.code_entry.empty() ? RV_PCLOADER_DEFAULT_CODE_ENTRY : manifest.budget.pccd.code_entry;
}

// Read a whole zip entry, refusing anything over max_size. Returns RV_OK on
// success, or RV_ERR_NOENT (no entry), RV_ERR_INVAL (oversized/corrupt),
// RV_ERR_NOMEM (allocation failure), RV_ERR_IO (read error) on failure;
// reason in `why`.
int read_whole_entry(const rv_zipreader &zip,
    const char *name,
    int64_t max_size,
    std::vector<unsigned char> &out,
    std::string &why);

// Directory route equivalent: read the WHOLE file at `path`, refusing
// anything over `max_size` before it becomes an allocation. Returns the
// reason as a string; empty means success.
std::string read_whole_file(const std::filesystem::path &path, int64_t max_size, std::vector<unsigned char> &out);

std::string extract_code(const std::vector<unsigned char> &code, std::string &out_path);

} // namespace rv_pcloader_detail
using namespace rv_pcloader_detail;
} // namespace rv_3dmppc
