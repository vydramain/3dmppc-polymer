#include "rv_pconsole/cd/rv_zipreader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_zip/rv_zip_format.hpp"

namespace rv_3dmppc {

namespace {

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
// output, polynomial 0xedb88320, pre- and post-inverted. It is written out here
// instead of being pulled from zlib because the whole point of the store-only
// container is that the console links no compression library at all; a 256-entry
// table is a cheaper dependency than any of them.
const std::array<uint32_t, 256>& crc_table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

// Running CRC: `state` is the value carried between chunks, starting at 0.
uint32_t crc32_update(uint32_t state, const void* data, std::size_t size) {
    const auto& table = crc_table();
    uint32_t c = state ^ 0xffffffffu;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xffu] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// Fields of one EOCD record, once validated.
struct rv_zip_eocd_fields {
    uint32_t cd_offset = 0;
    uint32_t cd_size = 0;
    uint16_t entries_total = 0;
};

// Decode `eocd` and reject anything this reader cannot handle (split
// archives, zip64, a directory that lies outside the file or is implausibly
// large). No member state needed beyond `file_size`, so this stays a free
// function rather than a method.
bool validate_eocd_record(const unsigned char* eocd, int64_t file_size, std::string& error,
                           rv_zip_eocd_fields& out) {
    const rv_pdklib::rv_zip_eocd rec =
        rv_pdklib::rv_zip_decode_eocd({eocd, rv_pdklib::rv_zip_eocd_size});
    const uint16_t disk = rec.disk_number;
    const uint16_t cd_disk = rec.cd_disk;
    const uint16_t entries_here = rec.entries_on_disk;
    const uint16_t entries_total = rec.entries_total;
    const uint32_t cd_size = rec.cd_size;
    const uint32_t cd_offset = rec.cd_offset;

    if (disk != 0 || cd_disk != 0 || entries_here != entries_total) {
        error = "split archives are not supported";
        return false;
    }
    if (entries_total == rv_pdklib::rv_zip_zip64_sentinel16 || cd_size == rv_pdklib::rv_zip_zip64_sentinel32 ||
        cd_offset == rv_pdklib::rv_zip_zip64_sentinel32) {
        error = "zip64 archives are not supported";
        return false;
    }

    // Every offset from here on is checked against the REAL file size before it
    // is used, and the subtraction form avoids the overflow that `a + b > size`
    // invites on 32-bit fields promoted to 64-bit arithmetic.
    if (static_cast<int64_t>(cd_offset) > file_size ||
        static_cast<int64_t>(cd_size) > file_size - static_cast<int64_t>(cd_offset)) {
        error = "central directory lies outside the file";
        return false;
    }
    if (static_cast<int64_t>(cd_size) > RV_PCZIP_MAX_DIRECTORY_BYTES) {
        error = "central directory is implausibly large";
        return false;
    }

    out.cd_offset = cd_offset;
    out.cd_size = cd_size;
    out.entries_total = entries_total;
    return true;
}

}  // namespace

bool rv_zipreader::open(const std::string& path, std::string& error) {
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
        return false;
    }

    file_.seekg(0, std::ios::end);
    const std::streamoff end = file_.tellg();
    if (!file_ || end < 0) {
        error = "cannot measure archive";
        file_.close();
        return false;
    }
    file_size_ = static_cast<int64_t>(end);

    if (file_size_ < static_cast<int64_t>(rv_pdklib::rv_zip_eocd_size)) {
        error = "file is smaller than an empty zip archive";
        file_.close();
        return false;
    }

    if (!parse_directory(error)) {
        entries_.clear();
        by_name_.clear();
        file_.close();
        file_.clear();
        return false;
    }

    ok_ = true;
    return true;
}

bool rv_zipreader::read_at(int64_t offset, void* dst, int64_t count) const {
    if (count == 0) return true;
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) return false;
    file_.read(static_cast<char*>(dst), static_cast<std::streamsize>(count));
    return file_.gcount() == static_cast<std::streamsize>(count);
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
bool rv_zipreader::find_eocd(const std::vector<unsigned char>& tail, std::size_t& pos) {
    if (tail.size() < rv_pdklib::rv_zip_eocd_size) return false;

    for (std::size_t i = tail.size() - rv_pdklib::rv_zip_eocd_size + 1; i-- > 0;) {
        const rv_pdklib::rv_zip_eocd rec =
            rv_pdklib::rv_zip_decode_eocd({tail.data() + i, rv_pdklib::rv_zip_eocd_size});
        if (rec.signature != rv_pdklib::rv_zip_sig_eocd) continue;

        const std::size_t comment_len = rec.comment_length;
        const std::size_t after = tail.size() - i - rv_pdklib::rv_zip_eocd_size;
        if (comment_len != after) continue;  // impostor: the tail does not add up

        pos = i;
        return true;
    }
    return false;
}

bool rv_zipreader::locate_eocd(std::string& error, std::vector<unsigned char>& tail, std::size_t& eocd_pos) const {
    const int64_t tail_size =
        std::min<int64_t>(file_size_, static_cast<int64_t>(rv_pdklib::rv_zip_eocd_size + rv_pdklib::rv_zip_max_comment_size));
    tail.resize(static_cast<std::size_t>(tail_size));
    if (!read_at(file_size_ - tail_size, tail.data(), tail_size)) {
        error = "cannot read the end of the archive";
        return false;
    }

    if (!find_eocd(tail, eocd_pos)) {
        error = "no end-of-central-directory record: this is not a zip archive";
        return false;
    }
    return true;
}

bool rv_zipreader::parse_directory(std::string& error) {
    std::vector<unsigned char> tail;
    std::size_t eocd_pos = 0;
    if (!locate_eocd(error, tail, eocd_pos)) return false;

    rv_zip_eocd_fields fields;
    if (!validate_eocd_record(tail.data() + eocd_pos, file_size_, error, fields)) return false;

    std::vector<unsigned char> cdir(static_cast<std::size_t>(fields.cd_size));
    if (!read_at(static_cast<int64_t>(fields.cd_offset), cdir.data(), static_cast<int64_t>(fields.cd_size))) {
        error = "cannot read the central directory (archive is truncated)";
        return false;
    }

    return parse_entries(cdir, fields.entries_total, error);
}

bool rv_zipreader::parse_entries(const std::vector<unsigned char>& cdir, uint16_t entries_total,
                                  std::string& error) {
    std::size_t p = 0;
    while (p + rv_pdklib::rv_zip_central_header_size <= cdir.size()) {
        const unsigned char* h = cdir.data() + p;
        const rv_pdklib::rv_zip_central_header ch =
            rv_pdklib::rv_zip_decode_central_header({h, rv_pdklib::rv_zip_central_header_size});
        if (ch.signature != rv_pdklib::rv_zip_sig_central) {
            // The directory is a chain of records; anything else in the chain
            // means the archive's own index is damaged, and guessing where the
            // next record might start is how a parser reads someone else's bytes.
            error = "damaged central directory record";
            return false;
        }

        const uint16_t method = ch.method;
        const uint32_t crc = ch.crc32;
        const uint32_t csize = ch.compressed_size;
        const uint32_t usize = ch.uncompressed_size;
        const std::size_t name_len = ch.name_length;
        const std::size_t extra_len = ch.extra_length;
        const std::size_t comment_len = ch.comment_length;
        const uint32_t lho = ch.local_header_offset;

        const std::size_t record =
            rv_pdklib::rv_zip_central_header_size + name_len + extra_len + comment_len;
        if (record > cdir.size() - p) {
            error = "central directory record runs past the directory";
            return false;
        }

        const std::string name(reinterpret_cast<const char*>(h + rv_pdklib::rv_zip_central_header_size), name_len);
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
            error = std::format(
                "entry '{}' uses compression method {}; this container is "
                "store-only (no decompressor on the console)",
                rv_pdklib::rv_log_escape(name.c_str()), method);
            return false;
        }
        if (csize == rv_pdklib::rv_zip_zip64_sentinel32 || usize == rv_pdklib::rv_zip_zip64_sentinel32 ||
            lho == rv_pdklib::rv_zip_zip64_sentinel32) {
            error = std::format("entry '{}' needs zip64, which is not supported",
                                rv_pdklib::rv_log_escape(name.c_str()));
            return false;
        }
        if (csize != usize) {
            error = std::format("entry '{}' is stored but its sizes disagree ({} vs {})",
                                rv_pdklib::rv_log_escape(name.c_str()), csize, usize);
            return false;
        }
        // The local header must at least FIT before its offset is ever seeked to.
        // The data bounds cannot be settled here - they depend on the local
        // header's own name/extra lengths - and are re-checked in read().
        if (static_cast<int64_t>(lho) > file_size_ - static_cast<int64_t>(rv_pdklib::rv_zip_local_header_size)) {
            error =
                std::format("entry '{}' points outside the archive", rv_pdklib::rv_log_escape(name.c_str()));
            return false;
        }

        if (entries_.size() >= RV_PCZIP_MAX_ENTRIES) {
            error = "archive declares implausibly many entries";
            return false;
        }

        if (by_name_.find(name) != by_name_.end()) {
            // Duplicate names are legal zip and ambiguous data. First one wins,
            // loudly: the alternative is a disc where which texture you get
            // depends on parse order.
            RV_LOG_WARN("pczip", "archive '{}' repeats entry '{}'; the later copy is ignored",
                        path_, rv_pdklib::rv_log_escape(name.c_str()));
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
        RV_LOG_WARN("pczip", "archive '{}' declares {} entries but {} were parsed", path_,
                    entries_total, entries_.size());
    }
    return true;
}

const rv_zipentry* rv_zipreader::find(const char* name) const {
    if (!ok_ || name == nullptr || name[0] == '\0') return nullptr;
    const auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) return nullptr;
    return &entries_[it->second];
}

int64_t rv_zipreader::size(const char* name) const {
    const rv_zipentry* entry = find(name);
    return entry == nullptr ? -1 : entry->size;
}

rv_zipread rv_zipreader::read(const char* name, void* baddr, int64_t cap, int64_t& nread) const {
    nread = 0;
    if (!ok_) return rv_zipread::io_error;
    if (cap < 0 || (baddr == nullptr && cap > 0)) return rv_zipread::short_buffer;

    const rv_zipentry* entry = find(name);
    if (entry == nullptr) return rv_zipread::not_found;

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
    if (size < 0 || lho < 0) return rv_zipread::corrupt;
    if (cap < size) return rv_zipread::short_buffer;
    if (size > 0 && baddr == nullptr) return rv_zipread::short_buffer;

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
    if (!read_at(lho, lh, static_cast<int64_t>(rv_pdklib::rv_zip_local_header_size))) {
        RV_LOG_ERR("pczip", "entry '{}': local header unreadable", rv_pdklib::rv_log_escape(name));
        return rv_zipread::corrupt;
    }
    const rv_pdklib::rv_zip_local_header lhdr =
        rv_pdklib::rv_zip_decode_local_header({lh, rv_pdklib::rv_zip_local_header_size});
    if (lhdr.signature != rv_pdklib::rv_zip_sig_local) {
        RV_LOG_ERR("pczip", "entry '{}': no local header at its offset", rv_pdklib::rv_log_escape(name));
        return rv_zipread::corrupt;
    }
    if (lhdr.method != rv_pdklib::rv_zip_method_store) {
        RV_LOG_ERR("pczip", "entry '{}': local header claims compression method {}",
                   rv_pdklib::rv_log_escape(name), lhdr.method);
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

    if (!read_at(data_offset, baddr, size)) {
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
    const unsigned char* p = static_cast<const unsigned char*>(baddr);
    for (int64_t done = 0; done < size; done += RV_PCZIP_CRC_CHUNK) {
        const int64_t chunk = std::min<int64_t>(RV_PCZIP_CRC_CHUNK, size - done);
        crc = crc32_update(crc, p + done, static_cast<std::size_t>(chunk));
    }
    if (crc != want_crc) {
        std::memset(baddr, 0, static_cast<std::size_t>(size));
        RV_LOG_ERR("pczip", "entry '{}': checksum mismatch (have {:08x}, expected {:08x})",
                   rv_pdklib::rv_log_escape(name), crc, want_crc);
        return rv_zipread::crc_mismatch;
    }

    nread = size;
    return rv_zipread::ok;
}

}  // namespace rv_3dmppc
