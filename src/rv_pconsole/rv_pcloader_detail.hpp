// Internals shared by rv_pcloader.cpp, rv_pcloader_mount.cpp,
// rv_pcloader_livedir_devtools.cpp and rv_pcloader_inspect.cpp.
#pragma once

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

// Which entry of the archive carries the code. The manifest names it; a
// manifest that leaves the key blank falls back to the conventional name.
// Both stages ask this, and they must agree: the version check reads the very
// entry the extraction later maps.
inline std::string code_entry_of(const rv_pdklib::rv_manifest &manifest)
{
    return manifest.budget.pccd.code_entry.empty() ? RV_PCLOADER_DEFAULT_CODE_ENTRY : manifest.budget.pccd.code_entry;
}

std::string read_whole_entry(const rv_zipreader &zip, const char *name,
    int64_t max_size,
    std::vector<unsigned char> &out);

// Same contract as read_whole_entry(), for the directory route: read the
// WHOLE file at `path`, refusing anything over `max_size` before it becomes
// an allocation. Empty return means success.
std::string read_whole_file(const std::filesystem::path &path,
    int64_t max_size,
    std::vector<unsigned char> &out);

std::string extract_code(const std::vector<unsigned char> &code,
    std::string &out_path);

} // namespace rv_pcloader_detail
using namespace rv_pcloader_detail;
} // namespace rv_3dmppc
