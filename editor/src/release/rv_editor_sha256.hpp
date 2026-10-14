#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rv_editor
{

// SHA-256 (FIPS 180-4) of a file's bytes as 64 lowercase hex digits, the form
// sha256sum prints; empty with the reason in `error` when it cannot be read.
std::string rv_editor_sha256_file(const std::filesystem::path &path, uint64_t &size, std::string &error);

} // namespace rv_editor
