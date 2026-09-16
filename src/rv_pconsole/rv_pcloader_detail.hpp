// Internals shared by rv_pcloader.cpp and rv_pcloader_code.cpp.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rv_pconsole/cd/rv_pczip.hpp"

namespace rv_3dmppc
{

namespace rv_pcloader_detail
{

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
