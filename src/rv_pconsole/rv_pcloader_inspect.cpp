// rv_pcloader: pre-dlopen inspection of the disc's code image - ELF check,
// PT_NOTE walk, version and checksum.
#include "rv_pconsole/rv_pcloader.hpp"

#include <elf.h>

#include <cstring>
#include <type_traits>
#include <vector>

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_disc_hash/rv_disc_hash.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pczip.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// ELF note name/desc fields are padded to a 4-byte boundary (elf(5)). The
// uint64_t parameter is the overflow guard: a 32-bit n_namesz/n_descsz widens
// at the call site, so `+ 3` can never wrap.
constexpr uint64_t align_elf_note_field_size(uint64_t size)
{
    return (size + 3ull) & ~3ull;
}

// Local hex formatting for a log line only - pdklib ships raw bytes, not text.
std::string bytes_to_hex(const unsigned char *bytes, std::size_t n)
{
    static const char *const digits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0f]);
    }
    return out;
}

template <typename O>
bool pod_peek(std::vector<unsigned char> &buf, int64_t off_start, int64_t off_end, O &out)
{
    static_assert(std::is_trivially_copyable_v<O>);

    if (off_start < 0 || off_start > (int64_t)buf.size() - (int64_t)sizeof(O)) {
        return false;
    }
    if (off_end < off_start || off_end > (int64_t)buf.size()) {
        return false;
    }

    std::memcpy(&out, buf.data() + off_start, sizeof(O));
    return true;
}

// ELF64 header: magic, class/byte order, e_type/e_machine, program-header
// stride. Each check legalises exactly the fields the next step relies on;
// until a check has passed, the fields it covers are just bytes. Fills
// out_ehdr on success.
bool elf_header_ok_(std::vector<unsigned char> &buffer, const char *info_entry,
    const char *origin, Elf64_Ehdr &out_ehdr)
{
    const int64_t size = static_cast<int64_t>(buffer.size());
    if (size <= 0) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' in '{}' is missing or empty; there is no binary "
            "to version-check",
            rv_pdklib::rv_log_escape(info_entry), rv_pdklib::rv_log_escape(origin));
        return false;
    }

    // The ELF header lives at offset 0 by definition - elf(5), "ELF header
    // (Ehdr)".
    if (!pod_peek(buffer, 0, sizeof(out_ehdr), out_ehdr)) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' is only {} bytes - smaller than an ELF64 header; "
            "not a loadable binary",
            rv_pdklib::rv_log_escape(info_entry), size);
        return false;
    }

    if (memcmp(out_ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' does not start with the ELF magic; it is not an "
            "ELF object at all",
            rv_pdklib::rv_log_escape(info_entry));
        return false;
    }

    // Single-byte fields, so they are readable regardless of byte order - and
    // only their verdict makes the multi-byte fields of the struct meaningful:
    // everything was copied under a little-endian assumption.
    if (out_ehdr.e_ident[EI_CLASS] != ELFCLASS64 || out_ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' is not a 64-bit little-endian ELF (class {}, "
            "data {}); this console only runs ELF64 LE discs",
            rv_pdklib::rv_log_escape(info_entry), (int)out_ehdr.e_ident[EI_CLASS],
            (int)out_ehdr.e_ident[EI_DATA]);
        return false;
    }

    if (out_ehdr.e_type != ET_DYN || out_ehdr.e_machine != EM_X86_64) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' is not an x86-64 shared object (e_type {}, "
            "e_machine {}); it cannot run on this console",
            rv_pdklib::rv_log_escape(info_entry), out_ehdr.e_type, out_ehdr.e_machine);
        return false;
    }

    // The program-header walk steps by e_phentsize; if the file declares a
    // different stride than the Elf64_Phdr we read with, every entry after the
    // first would be read misaligned with the table.
    if (out_ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' declares {}-byte program headers, elf(5) says {}; "
            "its segment table cannot be walked",
            rv_pdklib::rv_log_escape(info_entry), out_ehdr.e_phentsize, sizeof(Elf64_Phdr));
        return false;
    }

    return true;
}

// Walks ehdr's program headers for the first PT_NOTE segment. found stays
// false (RV_OK) when none exists; a bounds violation returns RV_ERR_INVAL
// immediately, already logged.
int64_t find_note_segment_(std::vector<unsigned char> &buffer, const Elf64_Ehdr &ehdr,
    uint64_t &segment_offset, uint64_t &segment_end, bool &found)
{
    found = false;
    Elf64_Phdr potential_note;
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        if (!pod_peek(buffer, ehdr.e_phoff + i * ehdr.e_phentsize,
                (ehdr.e_phoff + i * ehdr.e_phentsize) + sizeof(potential_note),
                potential_note)) {
            RV_LOG_WARN("pcloader", "can not to peek elf64_phdr from disc.so ");
            continue;
        }

        if (potential_note.p_type != PT_NOTE) {
            continue;
        }

        segment_offset = static_cast<uint64_t>(potential_note.p_offset);
        const uint64_t segment_size = static_cast<uint64_t>(potential_note.p_filesz);
        const uint64_t buffer_size = static_cast<uint64_t>(buffer.size());

        if (segment_offset > buffer_size || segment_size > buffer_size - segment_offset) {
            RV_LOG_ERR("pcloader", "ELF note segment exceeds mppcdisc bounds");
            return RV_ERR_INVAL;
        }

        segment_end = segment_offset + segment_size;
        found = true;
        return RV_OK;
    }
    return RV_OK;
}

// Walks the notes inside [segment_offset, segment_end) for the RV_MPPC note
// and fills version_info. found stays false (RV_OK) when no matching note is
// present; only an unreadable note header returns RV_ERR_INVAL.
int64_t read_mppc_note_(std::vector<unsigned char> &buffer, uint64_t segment_offset,
    uint64_t segment_end, rv_mppc_note_desc &version_info, bool &found)
{
    found = false;

    constexpr uint64_t expected_owner_size = sizeof(RV_MPPC_NOTE_OWNER);
    constexpr uint64_t expected_desc_size = sizeof(rv_mppc_note_desc);

    Elf64_Nhdr note{};

    uint64_t note_offset = segment_offset;
    uint64_t note_size = 0;

    for (;; note_offset += note_size) {
        if (note_offset >= segment_end) {
            break;
        }

        const uint64_t segment_bytes_left = segment_end - note_offset;

        if (segment_bytes_left < sizeof(note)) {
            RV_LOG_WARN("pcloader", "ELF note header exceeds note segment bounds");
            break;
        }

        const uint64_t note_header_end = note_offset + sizeof(note);

        if (!pod_peek(buffer, note_offset, note_header_end, note)) {
            RV_LOG_ERR("pcloader", "cannot read ELF note header from mppcdisc");
            return RV_ERR_INVAL;
        }

        const uint64_t owner_size = static_cast<uint64_t>(note.n_namesz);
        const uint64_t desc_size = static_cast<uint64_t>(note.n_descsz);

        const uint64_t aligned_owner_size = align_elf_note_field_size(owner_size);
        const uint64_t aligned_desc_size = align_elf_note_field_size(desc_size);

        note_size = sizeof(note) + aligned_owner_size + aligned_desc_size;

        if (note_size > segment_bytes_left) {
            RV_LOG_WARN("pcloader", "ELF note exceeds note segment bounds");
            break;
        }

        const bool header_matches = owner_size == expected_owner_size &&
            desc_size == expected_desc_size &&
            note.n_type == RV_MPPC_NOTE_TYPE;

        if (!header_matches) {
            // .so files carry notes from the toolchain (e.g. .note.gnu.build-id,
            // .note.ABI-tag), so a note that is not ours is skipped in silence.
            // The absence of the mppc note after the whole walk is what gets
            // reported as an error below.
            continue;
        }

        const uint64_t owner_offset = note_offset + sizeof(note);
        const uint64_t desc_offset = owner_offset + aligned_owner_size;
        const uint64_t note_end = note_offset + note_size;

        const auto *owner = buffer.data() + owner_offset;

        if (std::memcmp(owner, RV_MPPC_NOTE_OWNER, expected_owner_size) != 0) {
            continue;
        }

        if (!pod_peek(buffer, desc_offset, note_end, version_info)) {
            RV_LOG_WARN("pcloader", "cannot read version descriptor from ELF note");
            continue;
        }

        found = true;
        break;
    }

    return RV_OK;
}

// Compares version_info against this console's RV_MPPC_VER_MAJOR/MINOR.
bool version_compatible_(const rv_mppc_note_desc &version_info)
{
    if (RV_MPPC_VER_MAJOR != version_info.version_major ||
        RV_MPPC_VER_MINOR < version_info.version_minor) {
        RV_LOG_ERR("pcloader",
            "disc version are incompatible to currect console version: "
            "disc version is: {}.{}; ",
            version_info.version_major, version_info.version_minor);
        return false;
    }
    return true;
}

} // namespace

int64_t rv_pcloader::pre_dlopen_check(rv_zipreader *zip,
    const char *info_entry)
{
    int64_t size = zip->size(info_entry);
    if (size <= 0) {
        // TODO(rv_log_escape): 22 calls in this file. The console is its only
        // caller, so it does not belong in pdklib - find it a console-side home.
        RV_LOG_ERR(
            "pcloader",
            "code entry '{}' in '{}' is missing or empty; there is no binary "
            "to version-check",
            rv_pdklib::rv_log_escape(info_entry), rv_pdklib::rv_log_escape(zip->path().c_str()));
        return RV_ERR_INVAL;
    }

    // One bulk read of the whole entry: the zip reader verifies the CRC over
    // all of it, and no byte below is trusted before that verdict.
    int64_t nread;
    std::vector<unsigned char> buffer(size);
    rv_zipread zipread = zip->read(info_entry, buffer.data(), size, nread);
    if (zipread != rv_3dmppc::rv_zipread::ok || nread != size) {
        RV_LOG_ERR(
            "pcloader",
            "cannot read code entry '{}' from '{}' (zip verdict {}, {} of {} "
            "bytes); refusing a disc whose code cannot be inspected",
            rv_pdklib::rv_log_escape(info_entry), rv_pdklib::rv_log_escape(zip->path().c_str()),
            static_cast<int>(zipread), nread, size);
        return RV_ERR_INVAL;
    }

    return pre_dlopen_check_bytes(buffer, info_entry, zip->path().c_str());
}

// Recomputes the disc code checksum and compares it against version_info; the
// disc code checksum is recomputed over the very buffer the ELF above was
// parsed from, and compared against what the burner stamped into the note. A
// mismatch means the code was altered after burning - refuse it before
// dlopen ever sees the file.
bool rv_pcloader::checksum_matches_(std::vector<unsigned char> &buffer,
    const char *info_entry, const rv_mppc_note_desc &version_info)
{
    unsigned char computed_checksum[rv_pdklib::RV_DISC_HASH_BYTES];
    std::string hash_error;
    if (!rv_pdklib::rv_disc_hash_compute(buffer.data(), buffer.size(),
            computed_checksum, hash_error)) {
        RV_LOG_ERR("pcloader",
            "cannot checksum code entry '{}': {}",
            rv_pdklib::rv_log_escape(info_entry), hash_error);
        return false;
    }

    static_assert(sizeof(version_info.magic) == rv_pdklib::RV_DISC_HASH_BYTES);
    if (std::memcmp(computed_checksum, version_info.magic,
            rv_pdklib::RV_DISC_HASH_BYTES) != 0) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' checksum mismatch: expected {}, got {}; the disc "
            "code was altered after burning",
            rv_pdklib::rv_log_escape(info_entry),
            bytes_to_hex(reinterpret_cast<const unsigned char *>(version_info.magic),
                sizeof(version_info.magic)),
            bytes_to_hex(computed_checksum, sizeof(computed_checksum)));
        return false;
    }

    // Kept only now, after the comparison passed: a checksum that did not match
    // is not this disc's checksum, and reporting it would invite a client to
    // compare against a number that was refused.
    code_hash_ = bytes_to_hex(computed_checksum, sizeof(computed_checksum));
    return true;
}

// The core of pre_dlopen_check(): ELF header, PT_NOTE walk, version and
// checksum, all off bytes already in memory. `origin` names the archive or
// directory the bytes came from, for the log lines only - this function never
// reads anything itself, which is what lets mount() (via the wrapper above)
// and mount_dir() share it verbatim.
int64_t rv_pcloader::pre_dlopen_check_bytes(std::vector<unsigned char> &buffer,
    const char *info_entry, const char *origin)
{
    Elf64_Ehdr mppcdisc_ehdr;
    if (!elf_header_ok_(buffer, info_entry, origin, mppcdisc_ehdr)) {
        return RV_ERR_INVAL;
    }

    uint64_t segment_offset = 0;
    uint64_t segment_end = 0;
    bool note_segment_found = false;
    if (const int64_t rc = find_note_segment_(buffer, mppcdisc_ehdr, segment_offset,
            segment_end, note_segment_found);
        rc != RV_OK) {
        return rc;
    }

    rv_mppc_note_desc version_info;
    bool version_info_found_flag = false;
    if (note_segment_found) {
        if (const int64_t rc = read_mppc_note_(buffer, segment_offset, segment_end,
                version_info, version_info_found_flag);
            rc != RV_OK) {
            return rc;
        }
    }

    if (!version_info_found_flag) {
        RV_LOG_ERR("pcloader", "version did not found in mppcdisc");
        return RV_ERR_INVAL;
    }

    if (!checksum_matches_(buffer, info_entry, version_info)) {
        return RV_ERR_INVAL;
    }

    if (!version_compatible_(version_info)) {
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

} // namespace rv_3dmppc
