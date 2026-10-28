#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include "pdk/rv_err.h"

// Whole-file read; a short read is an I/O failure, not a shorter text.

namespace rv_pdklib
{

namespace detail
{

// Read buffer of rv_inspect_read_text, kept on the stack; a larger file takes several reads.
inline constexpr std::size_t rv_inspect_read_chunk_size = 4096;

} // namespace detail

// A short read is an I/O failure: a failing stream looks like the end of the data. The size compare
// assumes the reported size is the byte count, which holds for a regular file on a normal filesystem.
// A sysfs file reports 4096 and holds less, so it is refused.
inline int rv_inspect_read_text(const std::filesystem::path &file, std::string &text, std::string &error)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    std::ifstream in(file, std::ios::binary);
    if (ec || !in) {
        error = "cannot read '" + file.string() + "'";
        return RV_ERR_IO;
    }
    std::string data;
    char chunk[detail::rv_inspect_read_chunk_size];
    while (in) {
        in.read(chunk, static_cast<std::streamsize>(detail::rv_inspect_read_chunk_size));
        data.append(chunk, static_cast<std::size_t>(in.gcount()));
    }
    if (in.bad() || data.size() != size) {
        error = "cannot read '" + file.string() + "'";
        return RV_ERR_IO;
    }
    text = std::move(data);
    return RV_OK;
}

} // namespace rv_pdklib
