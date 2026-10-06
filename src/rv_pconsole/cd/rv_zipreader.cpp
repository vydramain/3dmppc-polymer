#include "rv_pconsole/cd/rv_zipreader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_zip/rv_zip_format.hpp"

namespace rv_3dmppc
{

namespace
{

// Sanity ceilings. Neither is a format limit; both exist so that a lying header
// costs a rejection rather than an allocation. The central directory is already
// bounded by the file size, but a 2 GiB "directory" inside a 2 GiB file is still
// a 2 GiB allocation, and no real disc index comes close to 32 MiB.
constexpr int64_t RV_PCZIP_MAX_DIRECTORY_BYTES = 32 * 1024 * 1024;
constexpr std::size_t RV_PCZIP_MAX_ENTRIES = 65536;

// Chunk used when streaming an entry's bytes through the CRC. Bounded so that
// verifying a large entry never doubles its memory cost.
constexpr int64_t RV_PCZIP_CRC_CHUNK = 64 * 1024;

// CRC-32/ISO-HDLC - the checksum zip stores. Reflected input and
// output, pre- and post-inverted. It is written out here instead of being
// pulled from zlib because the whole point of the store-only container is
// that the console links no compression library at all; a 256-entry table is
// a cheaper dependency than any of them.
const std::array<uint32_t, rv_pdklib::rv_zip_crc_table_size> &crc_table()
{
    static const std::array<uint32_t, rv_pdklib::rv_zip_crc_table_size> table = [] {
        std::array<uint32_t, rv_pdklib::rv_zip_crc_table_size> t{};
        for (uint32_t i = 0; i < rv_pdklib::rv_zip_crc_table_size; ++i) {
            uint32_t c = i;
            for (int k = 0; k < rv_pdklib::rv_zip_crc_bits_per_byte; ++k) {
                c = (c & 1u) ? (rv_pdklib::rv_zip_crc_polynomial ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

// Running CRC: `state` is the value carried between chunks, starting at 0.
uint32_t crc32_update(uint32_t state, const void *data, std::size_t size)
{
    const auto &table = crc_table();
    uint32_t c = state ^ rv_pdklib::rv_zip_crc_init_xor;
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i) {
        c = table[(c ^ p[i]) & rv_pdklib::rv_zip_crc_byte_mask] ^ (c >> rv_pdklib::rv_zip_crc_bits_per_byte);
    }
    return c ^ rv_pdklib::rv_zip_crc_init_xor;
}

} // namespace

int rv_zipreader::open(const std::string &path, std::string &error)
{
    ok_ = false;
    entries_.clear();
    by_name_.clear();
    file_.close();
    file_.clear();
    path_ = path;
    file_size_ = 0;

    file_.open(path, std::ios::binary);
    if (!file_) {
        error = "cannot open archive";
        return RV_ERR_IO;
    }

    file_.seekg(0, std::ios::end);
    const std::streamoff end = file_.tellg();
    if (!file_ || end < 0) {
        error = "cannot measure archive";
        file_.close();
        return RV_ERR_IO;
    }
    file_size_ = static_cast<int64_t>(end);

    if (file_size_ < static_cast<int64_t>(rv_pdklib::rv_zip_eocd_size)) {
        error = "file is smaller than an empty zip archive";
        file_.close();
        return RV_ERR_INVAL;
    }

    const int r = parse_directory(error);
    if (r != RV_OK) {
        entries_.clear();
        by_name_.clear();
        file_.close();
        file_.clear();
        return r;
    }

    ok_ = true;
    return RV_OK;
}

int rv_zipreader::read_at(int64_t offset, void *dst, int64_t count) const
{
    if (count == 0) {
        return RV_OK;
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) {
        return RV_ERR_IO;
    }
    file_.read(static_cast<char *>(dst), static_cast<std::streamsize>(count));
    return file_.gcount() == static_cast<std::streamsize>(count) ? RV_OK : RV_ERR_IO;
}

// Backward search for the End Of Central Directory record - the EOCD is
// the only way into a zip (it says where the central directory starts), and it is
// NOT simply the last 22 bytes. The record ends with a variable-length archive
// comment of up to 65535 bytes, so an archive with any comment at all puts the
// record further from the end, and "read the last 22 bytes" then finds nothing
// and declares a perfectly good archive corrupt. The signature must therefore be
// searched for, backwards, across the last rv_zip_eocd_size + rv_zip_max_comment_size bytes.
//
// Backwards, and not forwards, for a second reason: `PK\x05\x06` is four ordinary
// bytes that can also occur inside stored file data or inside the comment itself.
// A forward scan can stop on such an impostor; the real record is the LAST
// plausible one, so scanning from the end and taking the first hit is what makes
// the search deterministic. "Plausible" is then checked properly - the comment
// length field must account for exactly the bytes that follow the record, which
// is what tells a real EOCD from four coincidental bytes of a texture.
int rv_zipreader::find_eocd(const std::vector<unsigned char> &tail, std::size_t &pos)
{
    if (tail.size() < rv_pdklib::rv_zip_eocd_size) {
        return RV_ERR_INVAL;
    }

    for (std::size_t i = tail.size() - rv_pdklib::rv_zip_eocd_size + 1; i-- > 0;) {
        const rv_pdklib::rv_zip_eocd rec = rv_pdklib::rv_zip_decode_eocd({ tail.data() + i, rv_pdklib::rv_zip_eocd_size });
        if (rec.signature != rv_pdklib::rv_zip_sig_eocd) {
            continue;
        }

        const std::size_t comment_len = rec.comment_length;
        const std::size_t after = tail.size() - i - rv_pdklib::rv_zip_eocd_size;
        if (comment_len != after) {
            continue; // impostor: the tail does not add up
        }

        pos = i;
        return RV_OK;
    }
    return RV_ERR_INVAL;
}

int rv_zipreader::locate_eocd(std::string &error, std::vector<unsigned char> &tail, std::size_t &eocd_pos) const
{
    const int64_t tail_size =
        std::min<int64_t>(file_size_, static_cast<int64_t>(rv_pdklib::rv_zip_eocd_size + rv_pdklib::rv_zip_max_comment_size));
    tail.resize(static_cast<std::size_t>(tail_size));
    int r = read_at(file_size_ - tail_size, tail.data(), tail_size);
    if (r != RV_OK) {
        error = "cannot read the end of the archive";
        return r;
    }

    r = find_eocd(tail, eocd_pos);
    if (r != RV_OK) {
        error = "no end-of-central-directory record: this is not a zip archive";
        return r;
    }
    return RV_OK;
}

int rv_zipreader::parse_directory(std::string &error)
{
    std::vector<unsigned char> tail;
    std::size_t eocd_pos = 0;
    int r = locate_eocd(error, tail, eocd_pos);
    if (r != RV_OK) {
        return r;
    }

    const rv_pdklib::rv_zip_eocd_result validated =
        rv_pdklib::rv_zip_validate_eocd({ tail.data() + eocd_pos, rv_pdklib::rv_zip_eocd_size },
            file_size_,
            RV_PCZIP_MAX_DIRECTORY_BYTES);
    switch (validated.status) {
    case rv_pdklib::rv_zip_eocd_status::ok:
        break;
    case rv_pdklib::rv_zip_eocd_status::split_archive:
        error = "split archives are not supported";
        return RV_ERR_INVAL;
    case rv_pdklib::rv_zip_eocd_status::zip64:
        error = "zip64 archives are not supported";
        return RV_ERR_INVAL;
    case rv_pdklib::rv_zip_eocd_status::directory_outside_file:
        error = "central directory lies outside the file";
        return RV_ERR_INVAL;
    case rv_pdklib::rv_zip_eocd_status::directory_too_large:
        error = "central directory is implausibly large";
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_zip_eocd_fields &fields = validated.fields;

    std::vector<unsigned char> cdir(static_cast<std::size_t>(fields.cd_size));
    r = read_at(static_cast<int64_t>(fields.cd_offset), cdir.data(), static_cast<int64_t>(fields.cd_size));
    if (r != RV_OK) {
        error = "cannot read the central directory (archive is truncated)";
        return r;
    }

    return parse_entries(cdir, fields.entries_total, error);
}

int rv_zipreader::parse_entries(const std::vector<unsigned char> &cdir, uint16_t entries_total, std::string &error)
{
    std::size_t p = 0;
    while (p + rv_pdklib::rv_zip_central_header_size <= cdir.size()) {
        const unsigned char *h = cdir.data() + p;
        const rv_pdklib::rv_zip_central_header ch =
            rv_pdklib::rv_zip_decode_central_header({ h, rv_pdklib::rv_zip_central_header_size });
        if (ch.signature != rv_pdklib::rv_zip_sig_central) {
            // The directory is a chain of records; anything else in the chain
            // means the archive's own index is damaged, and guessing where the
            // next record might start is how a parser reads someone else's bytes.
            error = "damaged central directory record";
            return RV_ERR_INVAL;
        }

        const uint16_t method = ch.method;
        const uint32_t crc = ch.crc32;
        const uint32_t csize = ch.compressed_size;
        const uint32_t usize = ch.uncompressed_size;
        const std::size_t name_len = ch.name_length;
        const std::size_t extra_len = ch.extra_length;
        const std::size_t comment_len = ch.comment_length;
        const uint32_t lho = ch.local_header_offset;

        const std::size_t record = rv_pdklib::rv_zip_central_header_size + name_len + extra_len + comment_len;
        if (record > cdir.size() - p) {
            error = "central directory record runs past the directory";
            return RV_ERR_INVAL;
        }

        const std::string name(reinterpret_cast<const char *>(h + rv_pdklib::rv_zip_central_header_size), name_len);
        p += record;

        if (name.empty()) {
            RV_LOG_WARN("pczip", "archive '{}' has an entry with an empty name; ignored", path_);
            continue;
        }

        if (method != rv_pdklib::rv_zip_method_store) {
            // The container is store-only by design (see the header). Refusing
            // the whole archive - rather than skipping the entry - is deliberate:
            // a disc whose assets are compressed was not burned for this console,
            // and letting it half-mount would turn one clear message into a
            // scattering of RV_ERR_NOENT during play.
            //
            // TODO(rv_log_escape): 12 calls in this file. The console is its only
            // caller, so it does not belong in pdklib - find it a console-side home.
            error = std::format("entry '{}' uses compression method {}; this container is "
                                "store-only (no decompressor on the console)",
                rv_pdklib::rv_log_escape(name.c_str()),
                method);
            return RV_ERR_INVAL;
        }
        if (csize == rv_pdklib::rv_zip_zip64_sentinel32 || usize == rv_pdklib::rv_zip_zip64_sentinel32 ||
            lho == rv_pdklib::rv_zip_zip64_sentinel32) {
            error = std::format("entry '{}' needs zip64, which is not supported", rv_pdklib::rv_log_escape(name.c_str()));
            return RV_ERR_INVAL;
        }
        if (csize != usize) {
            error = std::format("entry '{}' is stored but its sizes disagree ({} vs {})",
                rv_pdklib::rv_log_escape(name.c_str()),
                csize,
                usize);
            return RV_ERR_INVAL;
        }
        // The local header must at least FIT before its offset is ever seeked to.
        // The data bounds cannot be settled here - they depend on the local
        // header's own name/extra lengths - and are re-checked in read().
        if (static_cast<int64_t>(lho) > file_size_ - static_cast<int64_t>(rv_pdklib::rv_zip_local_header_size)) {
            error = std::format("entry '{}' points outside the archive", rv_pdklib::rv_log_escape(name.c_str()));
            return RV_ERR_INVAL;
        }

        if (entries_.size() >= RV_PCZIP_MAX_ENTRIES) {
            error = "archive declares implausibly many entries";
            return RV_ERR_INVAL;
        }

        if (by_name_.find(name) != by_name_.end()) {
            // Duplicate names are legal zip and ambiguous data. First one wins,
            // loudly: the alternative is a disc where which texture you get
            // depends on parse order.
            RV_LOG_WARN("pczip",
                "archive '{}' repeats entry '{}'; the later copy is ignored",
                path_,
                rv_pdklib::rv_log_escape(name.c_str()));
            continue;
        }

        rv_zipentry entry;
        entry.name = name;
        entry.local_header_offset = static_cast<int64_t>(lho);
        entry.size = static_cast<int64_t>(usize);
        entry.crc32 = crc;
        by_name_.emplace(entry.name, entries_.size());
        entries_.push_back(std::move(entry));
    }

    if (entries_.size() != static_cast<std::size_t>(entries_total)) {
        // Not fatal: what was actually parsed is what exists. Worth saying,
        // because it is the fingerprint of a truncated or hand-edited archive.
        RV_LOG_WARN("pczip", "archive '{}' declares {} entries but {} were parsed", path_, entries_total, entries_.size());
    }
    return RV_OK;
}

const rv_zipentry *rv_zipreader::find(const char *name) const
{
    if (!ok_ || name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    const auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) {
        return nullptr;
    }
    return &entries_[it->second];
}

int64_t rv_zipreader::size(const char *name) const
{
    const rv_zipentry *entry = find(name);
    return entry == nullptr ? -1 : entry->size;
}

rv_zipread rv_zipreader::read(const char *name, void *baddr, int64_t cap, int64_t &nread) const
{
    nread = 0;
    if (!ok_) {
        return rv_zipread::io_error;
    }
    if (cap < 0 || (baddr == nullptr && cap > 0)) {
        return rv_zipread::short_buffer;
    }

    const rv_zipentry *entry = find(name);
    if (entry == nullptr) {
        return rv_zipread::not_found;
    }

    // The directory record is COPIED out before a single byte lands in `baddr`.
    // `baddr` is a void* the caller owns, and nothing stops it from overlapping
    // this reader's own storage; re-reading `entry->size` after the transfer
    // would then mean re-reading a field the transfer just rewrote, and every
    // bound checked above would be a bound about the past. The locals are also
    // what makes each cast to size_t provably in range.
    const int64_t lho = entry->local_header_offset;
    const int64_t size = entry->size;
    const uint32_t want_crc = entry->crc32;
    // Unreachable by construction - a size comes from a uint32 field - but the
    // check is what turns "by construction" into something the compiler and the
    // next reader can both see.
    if (size < 0 || lho < 0) {
        return rv_zipread::corrupt;
    }
    if (cap < size) {
        return rv_zipread::short_buffer;
    }
    if (size > 0 && baddr == nullptr) {
        return rv_zipread::short_buffer;
    }

    // The data offset comes from the LOCAL header, never from the
    // central directory. The two headers describe the same entry twice, and the
    // lengths of their `name` and `extra` fields are INDEPENDENT: writers
    // routinely put a zip64/timestamp/unix-uid extra field in one and not the
    // other, so `local_header_offset + 46 + cdir_name_len + cdir_extra_len` is
    // not where the bytes are. The only correct route is to seek to
    // local_header_offset, read the 30-byte fixed local header, and take ITS
    // name/extra lengths: data begins at lho + 30 + local_name_len +
    // local_extra_len. Computing it from the central directory instead is the
    // classic zip-reader bug - it lands a few bytes off and yields plausible
    // garbage rather than an error, which is the worst possible failure mode for
    // a texture or a model.
    unsigned char lh[rv_pdklib::rv_zip_local_header_size];
    if (read_at(lho, lh, static_cast<int64_t>(rv_pdklib::rv_zip_local_header_size)) != RV_OK) {
        RV_LOG_ERR("pczip", "entry '{}': local header unreadable", rv_pdklib::rv_log_escape(name));
        return rv_zipread::corrupt;
    }
    const rv_pdklib::rv_zip_local_header lhdr =
        rv_pdklib::rv_zip_decode_local_header({ lh, rv_pdklib::rv_zip_local_header_size });
    if (lhdr.signature != rv_pdklib::rv_zip_sig_local) {
        RV_LOG_ERR("pczip", "entry '{}': no local header at its offset", rv_pdklib::rv_log_escape(name));
        return rv_zipread::corrupt;
    }
    if (lhdr.method != rv_pdklib::rv_zip_method_store) {
        RV_LOG_ERR("pczip",
            "entry '{}': local header claims compression method {}",
            rv_pdklib::rv_log_escape(name),
            lhdr.method);
        return rv_zipread::corrupt;
    }

    const int64_t local_name_len = lhdr.name_length;
    const int64_t local_extra_len = lhdr.extra_length;
    const int64_t data_offset =
        lho + static_cast<int64_t>(rv_pdklib::rv_zip_local_header_size) + local_name_len + local_extra_len;

    // Bounds first, seek second - always in that order, and phrased as
    // subtraction so nothing can wrap.
    if (data_offset > file_size_ || size > file_size_ - data_offset) {
        RV_LOG_ERR("pczip", "entry '{}': data lies outside the archive", rv_pdklib::rv_log_escape(name));
        return rv_zipread::corrupt;
    }

    if (size == 0) {
        // An empty entry is a legal entry. Its CRC is 0 by definition; anything
        // else means the directory is lying about it.
        if (want_crc != 0) {
            RV_LOG_ERR("pczip", "entry '{}': empty but claims a checksum", rv_pdklib::rv_log_escape(name));
            return rv_zipread::crc_mismatch;
        }
        return rv_zipread::ok;
    }

    if (read_at(data_offset, baddr, size) != RV_OK) {
        std::memset(baddr, 0, static_cast<std::size_t>(size));
        RV_LOG_ERR("pczip", "entry '{}': short read of {} bytes", rv_pdklib::rv_log_escape(name), size);
        return rv_zipread::io_error;
    }

    // Verify what was just delivered. This is the cheap half of "the disc may be
    // rotten": the structure checks above catch an archive that lies about WHERE
    // the bytes are, and the CRC catches one that is honest about the location
    // and wrong about the contents - a flipped bit in storage, a truncated
    // download, a half-finished burn. Without it such a disc renders as noise and
    // gets reported as a game bug.
    uint32_t crc = 0;
    const unsigned char *p = static_cast<const unsigned char *>(baddr);
    for (int64_t done = 0; done < size; done += RV_PCZIP_CRC_CHUNK) {
        const int64_t chunk = std::min<int64_t>(RV_PCZIP_CRC_CHUNK, size - done);
        crc = crc32_update(crc, p + done, static_cast<std::size_t>(chunk));
    }
    if (crc != want_crc) {
        std::memset(baddr, 0, static_cast<std::size_t>(size));
        RV_LOG_ERR("pczip",
            "entry '{}': checksum mismatch (have {:08x}, expected {:08x})",
            rv_pdklib::rv_log_escape(name),
            crc,
            want_crc);
        return rv_zipread::crc_mismatch;
    }

    nread = size;
    return rv_zipread::ok;
}

} // namespace rv_3dmppc
