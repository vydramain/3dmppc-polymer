#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rv_pdktools
{

// --- reading a .mppcdisc back ---
//
// Deliberately small, and deliberately not hardened. The central directory gives
// names, sizes and offsets, and that is everything the tools ask of an archive
// they are only reporting on. The bounds-checked, CRC-verifying, hostile-input
// reader lives on the console side (src/rv_pconsole/cd/rv_pczip.*), because that
// is where an archive that came from somewhere else gets opened. Here the input
// is a file the developer produced on their own machine a moment ago.
//
// The whole file is held in memory: a .mppcdisc is one disc image, the caller
// wants both the listing and some of the contents, and a second pass over the
// file would buy nothing.

/// One entry, as the central directory describes it.
struct zip_read_entry {
    std::string name;         ///< the flat name the console will ask for
    int64_t size = 0;         ///< uncompressed size; equals the stored size
    int64_t local_offset = 0; ///< where this entry's local header begins
    int method = 0;           ///< compression method; a .mppcdisc uses store only
};

/// An opened archive: its bytes, and the directory that indexes them.
struct zip_archive {
    std::vector<unsigned char> bytes;    ///< the whole file
    std::vector<zip_read_entry> entries; ///< in central-directory order
};

/// Read an archive from disk and parse its central directory.
///
/// @param path   the .mppcdisc to open
/// @param out    receives the bytes and the entry list
/// @param error  set when the file cannot be read or is not a zip archive
/// @return true when @p out is usable
bool zip_open(const std::filesystem::path &path, zip_archive &out, std::string &error);

/// Find one entry by its exact name.
///
/// @param archive  an opened archive
/// @param name     the flat name to look for
/// @return the entry, or nullptr when the archive has no such name
const zip_read_entry *zip_find(const zip_archive &archive, const std::string &name);

/// Copy out the bytes of one entry.
///
/// @param archive  the archive @p entry came from
/// @param entry    the entry to read
/// @param out      receives the entry's contents
/// @param error    set when the entry is compressed or its extent is wrong
/// @return true when @p out holds the entry's bytes
bool zip_entry_bytes(
    const zip_archive &archive,
    const zip_read_entry &entry,
    std::string &out,
    std::string &error);

} // namespace rv_pdktools
