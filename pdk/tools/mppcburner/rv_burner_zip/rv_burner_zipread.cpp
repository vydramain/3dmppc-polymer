#include "rv_burner_zipread.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <ios>

#include "rv_burner_common/rv_burner_bytes.hpp"
#include "rv_burner_zip/rv_burner_zip_format.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// Read a whole file into memory. Opened at the end (`ate`) so tellg gives the
// size before a single byte is read, which is what lets the buffer be sized once
// instead of grown.
static bool read_whole_file(
    const fs::path &path,
    std::vector<unsigned char> &out,
    std::string &error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size < 0) {
        error = "cannot size '" + path.string() + "'";
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!out.empty()) {
        file.read(reinterpret_cast<char *>(out.data()), size);
        if (!file) {
            error = "short read on '" + path.string() + "'";
            return false;
        }
    }

    return true;
}

// Parse the central directory of an archive already in memory.
static bool zip_list(
    const std::vector<unsigned char> &bytes,
    std::vector<zip_read_entry> &out,
    std::string &error)
{
    if (bytes.size() < k_eocd_size) {
        error = "file is too small to be a zip archive";
        return false;
    }

    // --- find the end-of-central-directory record ---
    //
    // It is the last record in the file, but a comment of up to 64 KiB may
    // follow it, so it is found by scanning backwards for its signature rather
    // than by seeking to a fixed offset.
    std::size_t eocd = 0;
    bool found = false;
    const std::size_t limit = std::min(bytes.size(), k_eocd_size + k_max_comment_size);
    for (std::size_t back = k_eocd_size; back <= limit; ++back) {
        const std::size_t at = bytes.size() - back;
        if (read_le_u32(bytes.data() + at + ZIP_EOCD_OFF_SIG) == k_sig_eocd) {
            eocd = at;
            found = true;
            break;
        }
    }
    if (!found) {
        error = "no end-of-central-directory record: not a zip archive";
        return false;
    }

    // --- walk the directory ---

    const uint16_t count = read_le_u16(bytes.data() + eocd + ZIP_EOCD_OFF_ENTRY_COUNT);
    const uint32_t directory_size = read_le_u32(bytes.data() + eocd + ZIP_EOCD_OFF_DIRECTORY_SIZE);
    const uint32_t directory_offset =
        read_le_u32(bytes.data() + eocd + ZIP_EOCD_OFF_DIRECTORY_OFFSET);
    if (static_cast<std::size_t>(directory_offset) + directory_size > bytes.size()) {
        error = "central directory runs past the end of the file";
        return false;
    }

    std::size_t at = directory_offset;
    for (uint16_t i = 0; i < count; ++i) {
        // Checked before every read: the count and the offsets come from the
        // file itself, so a truncated or edited archive must not be trusted to
        // stay inside its own bounds.
        if (at + k_central_header_size > bytes.size() ||
            read_le_u32(bytes.data() + at + ZIP_CENTRAL_OFF_SIG) != k_sig_central) {
            error = "central directory entry " + std::to_string(i) + " is malformed";
            return false;
        }

        zip_read_entry entry;
        entry.method = read_le_u16(bytes.data() + at + ZIP_CENTRAL_OFF_METHOD);
        entry.size = read_le_u32(bytes.data() + at + ZIP_CENTRAL_OFF_SIZE);
        entry.local_offset = read_le_u32(bytes.data() + at + ZIP_CENTRAL_OFF_LOCAL_OFFSET);

        const uint16_t name_length = read_le_u16(bytes.data() + at + ZIP_CENTRAL_OFF_NAME_LENGTH);
        const uint16_t extra_length = read_le_u16(bytes.data() + at + ZIP_CENTRAL_OFF_EXTRA_LENGTH);
        const uint16_t comment_length =
            read_le_u16(bytes.data() + at + ZIP_CENTRAL_OFF_COMMENT_LENGTH);

        if (at + k_central_header_size + name_length > bytes.size()) {
            error = "central directory entry " + std::to_string(i) + " has a runaway name";
            return false;
        }
        entry.name.assign(
            reinterpret_cast<const char *>(bytes.data()) + at + k_central_header_size,
            name_length);

        out.push_back(entry);
        at += k_central_header_size + name_length + extra_length + comment_length;
    }

    return true;
}

} // namespace rv_pdktools

bool rv_pdktools::zip_open(const fs::path &path, zip_archive &out, std::string &error)
{
    zip_archive archive;
    if (!read_whole_file(path, archive.bytes, error)) {
        return false;
    }
    if (!zip_list(archive.bytes, archive.entries, error)) {
        return false;
    }

    out = std::move(archive);
    return true;
}

const rv_pdktools::zip_read_entry *rv_pdktools::zip_find(
    const zip_archive &archive,
    const std::string &name)
{
    for (const zip_read_entry &entry : archive.entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool rv_pdktools::zip_entry_bytes(
    const zip_archive &archive,
    const zip_read_entry &entry,
    std::string &out,
    std::string &error)
{
    const std::vector<unsigned char> &bytes = archive.bytes;
    const std::size_t at = static_cast<std::size_t>(entry.local_offset);

    // The central directory said the local header is here; the local header has
    // to agree. They disagree in exactly one interesting case — an archive
    // edited or truncated after it was written.
    if (at + k_local_header_size > bytes.size() ||
        read_le_u32(bytes.data() + at + ZIP_LOCAL_OFF_SIG) != k_sig_local) {
        error = "entry '" + entry.name + "' has no local header where the directory says";
        return false;
    }

    if (entry.method != k_method_store) {
        error = "entry '" + entry.name + "' is compressed; .mppcdisc is store-only";
        return false;
    }

    // The name and extra fields sit between the header and the data, and their
    // lengths are the local header's own, not the directory's — the two are
    // allowed to differ in the extra field.
    const std::size_t data = at + k_local_header_size +
        read_le_u16(bytes.data() + at + ZIP_LOCAL_OFF_NAME_LENGTH) +
        read_le_u16(bytes.data() + at + ZIP_LOCAL_OFF_EXTRA_LENGTH);
    if (data + static_cast<std::size_t>(entry.size) > bytes.size()) {
        error = "entry '" + entry.name + "' runs past the end of the file";
        return false;
    }

    out.assign(reinterpret_cast<const char *>(bytes.data()) + data,
        static_cast<std::size_t>(entry.size));
    return true;
}
