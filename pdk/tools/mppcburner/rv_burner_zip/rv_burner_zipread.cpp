#include "rv_burner_zipread.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <ios>

#include "pdklib/rv_zip/rv_zip_format.hpp"

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
    if (bytes.size() < rv_pdklib::rv_zip_eocd_size) {
        error = "file is too small to be a zip archive";
        return false;
    }

    // --- find the end-of-central-directory record ---
    //
    // It is the last record in the file, but a comment of up to 64 KiB may
    // follow it, so it is found by scanning backwards for its signature rather
    // than by seeking to a fixed offset. Unlike the console's reader, this one
    // does not also check that the comment-length field matches the bytes that
    // actually follow the record: this tool trusts a file the developer just
    // produced, and the extra check is not worth restating here.
    std::size_t eocd = 0;
    bool found = false;
    const std::size_t limit = std::min(bytes.size(), rv_pdklib::rv_zip_eocd_size + rv_pdklib::rv_zip_max_comment_size);
    for (std::size_t back = rv_pdklib::rv_zip_eocd_size; back <= limit; ++back) {
        const std::size_t at = bytes.size() - back;
        const rv_pdklib::rv_zip_eocd rec =
            rv_pdklib::rv_zip_decode_eocd({bytes.data() + at, rv_pdklib::rv_zip_eocd_size});
        if (rec.signature == rv_pdklib::rv_zip_sig_eocd) {
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

    const rv_pdklib::rv_zip_eocd eocd_rec =
        rv_pdklib::rv_zip_decode_eocd({bytes.data() + eocd, rv_pdklib::rv_zip_eocd_size});
    const uint16_t count = eocd_rec.entries_total;
    const uint32_t directory_size = eocd_rec.cd_size;
    const uint32_t directory_offset = eocd_rec.cd_offset;
    if (static_cast<std::size_t>(directory_offset) + directory_size > bytes.size()) {
        error = "central directory runs past the end of the file";
        return false;
    }

    std::size_t at = directory_offset;
    for (uint16_t i = 0; i < count; ++i) {
        // Checked before every read: the count and the offsets come from the
        // file itself, so a truncated or edited archive must not be trusted to
        // stay inside its own bounds.
        if (at + rv_pdklib::rv_zip_central_header_size > bytes.size()) {
            error = "central directory entry " + std::to_string(i) + " is malformed";
            return false;
        }
        const rv_pdklib::rv_zip_central_header ch =
            rv_pdklib::rv_zip_decode_central_header({bytes.data() + at, rv_pdklib::rv_zip_central_header_size});
        if (ch.signature != rv_pdklib::rv_zip_sig_central) {
            error = "central directory entry " + std::to_string(i) + " is malformed";
            return false;
        }

        zip_read_entry entry;
        entry.method = ch.method;
        entry.size = ch.uncompressed_size;
        entry.local_offset = ch.local_header_offset;

        const uint16_t name_length = ch.name_length;
        const uint16_t extra_length = ch.extra_length;
        const uint16_t comment_length = ch.comment_length;

        if (at + rv_pdklib::rv_zip_central_header_size + name_length > bytes.size()) {
            error = "central directory entry " + std::to_string(i) + " has a runaway name";
            return false;
        }
        entry.name.assign(
            reinterpret_cast<const char *>(bytes.data()) + at + rv_pdklib::rv_zip_central_header_size,
            name_length);

        out.push_back(entry);
        at += rv_pdklib::rv_zip_central_header_size + name_length + extra_length + comment_length;
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
    if (at + rv_pdklib::rv_zip_local_header_size > bytes.size()) {
        error = "entry '" + entry.name + "' has no local header where the directory says";
        return false;
    }
    const rv_pdklib::rv_zip_local_header lh =
        rv_pdklib::rv_zip_decode_local_header({bytes.data() + at, rv_pdklib::rv_zip_local_header_size});
    if (lh.signature != rv_pdklib::rv_zip_sig_local) {
        error = "entry '" + entry.name + "' has no local header where the directory says";
        return false;
    }

    if (entry.method != rv_pdklib::rv_zip_method_store) {
        error = "entry '" + entry.name + "' is compressed; .mppcdisc is store-only";
        return false;
    }

    // The name and extra fields sit between the header and the data, and their
    // lengths are the local header's own, not the directory's — the two are
    // allowed to differ in the extra field.
    const std::size_t data = at + rv_pdklib::rv_zip_local_header_size + lh.name_length + lh.extra_length;
    if (data + static_cast<std::size_t>(entry.size) > bytes.size()) {
        error = "entry '" + entry.name + "' runs past the end of the file";
        return false;
    }

    out.assign(reinterpret_cast<const char *>(bytes.data()) + data,
        static_cast<std::size_t>(entry.size));
    return true;
}
