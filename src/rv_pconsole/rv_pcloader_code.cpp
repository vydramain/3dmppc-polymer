// rv_pcloader: the disc's code image before dlopen - ELF check, staging, extraction.
#include "rv_pconsole/rv_pcloader.hpp"

#include <elf.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
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

// TMPDIR or /tmp, trailing slashes trimmed. The one place both extract_code()
// and rv_pcloader_probe_staging() decide where the extracted disc.so lives —
// factored out so the two can never drift onto different directories.
std::string staging_dir()
{
    const char *tmpdir = std::getenv("TMPDIR");
    std::string dir =
        (tmpdir != nullptr && *tmpdir != '\0') ? std::string(tmpdir) : "/tmp";
    while (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }
    return dir;
}

} // namespace

namespace rv_pcloader_detail
{

// Write `code` to a fresh private file and hand back its path.
//
// mkstemp is what makes the name unpredictable: the console must not open a
// path an attacker could have guessed and pre-created as a symlink to something
// else, which is exactly the classic /tmp race. mkstemp creates with O_EXCL and
// mode 0600, and the fchmod below adds only the execute bit the mapping needs —
// 0700 total, this user and nobody else. A group- or world-writable staging
// file would be a way to swap the disc's code out between this write and the
// dlopen a few lines later.
std::string extract_code(const std::vector<unsigned char> &code,
    std::string &out_path)
{
    const std::string dir = staging_dir();

    std::string tmpl = dir + "/mppcdisc-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    const int fd = ::mkstemp(name.data());
    if (fd < 0) {
        return std::format("cannot create a staging file in '{}': {}", dir,
            std::strerror(errno));
    }

    // From here on the file exists, so every failure path must remove it.
    out_path.assign(name.data());

    std::string error;
    if (::fchmod(fd, S_IRWXU) != 0) {
        error = std::format("cannot make the staging file executable: {}",
            std::strerror(errno));
    }

    std::size_t written = 0;
    while (error.empty() && written < code.size()) {
        const ssize_t rc =
            ::write(fd, code.data() + written, code.size() - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::format("cannot write the staging file: {}",
                std::strerror(errno));
            break;
        }
        if (rc == 0) {
            error = "the staging file accepted no bytes";
            break;
        }
        written += static_cast<std::size_t>(rc);
    }

    if (::close(fd) != 0 && error.empty()) {
        // A close() that fails is a write that did not land — the mapping the
        // loader is about to make would be of a truncated object file.
        error =
            std::format("cannot close the staging file: {}", std::strerror(errno));
    }

    if (!error.empty()) {
        ::unlink(out_path.c_str());
        out_path.clear();
    }
    return error;
}

} // namespace rv_pcloader_detail

namespace
{

// ELF note name/desc fields are padded to a 4-byte boundary (elf(5)). The
// uint64_t parameter is the overflow guard: a 32-bit n_namesz/n_descsz widens
// at the call site, so `+ 3` can never wrap.
constexpr uint64_t align_elf_note_field_size(uint64_t size)
{
    return (size + 3ull) & ~3ull;
}

// Local hex formatting for a log line only — pdklib ships raw bytes, not text.
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

} // namespace

// Stage C: is the staging area an extracted disc.so will need actually
// usable? Creates and removes a probe file in the same directory
// extract_code() would use (staging_dir(), above — the one helper both this
// function and extract_code() share, so they can never disagree on the
// directory). Returns RV_OK, or a negative rv_err after logging the
// directory and why it cannot be used.
int64_t rv_pcloader_probe_staging()
{
    const std::string dir = staging_dir();

    std::string tmpl = dir + "/mppcdisc-probe-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    const int fd = ::mkstemp(name.data());
    if (fd < 0) {
        RV_LOG_ERR("pcloader",
            "staging directory '{}' cannot be used to extract a disc's code: {}",
            dir, std::strerror(errno));
        return RV_ERR_IO;
    }
    ::close(fd);

    if (::unlink(name.data()) != 0 && errno != ENOENT) {
        RV_LOG_ERR("pcloader",
            "staging directory '{}' accepted a probe file but would not remove it: {}",
            dir, std::strerror(errno));
        return RV_ERR_IO;
    }
    return RV_OK;
}

template <typename O>
bool rv_pcloader::pod_peek(std::vector<unsigned char> &buf, int64_t off_start,
    int64_t off_end, O &out)
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

int64_t rv_pcloader::pre_dlopen_check(rv_zipreader *zip,
    const char *info_entry)
{
    int64_t size = zip->size(info_entry);
    if (size <= 0) {
        // TODO(rv_log_escape): 22 calls in this file. The console is its only
        // caller, so it does not belong in pdklib — find it a console-side home.
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

    // The ELF header lives at offset 0 by definition — elf(5), "ELF header
    // (Ehdr)". Each check below legalises exactly the fields the next step
    // relies on; until a check has passed, the fields it covers are just bytes.
    Elf64_Ehdr mppcdisc_ehdr;
    if (!pod_peek(buffer, 0, sizeof(mppcdisc_ehdr), mppcdisc_ehdr)) {
        RV_LOG_ERR(
            "pcloader",
            "code entry '{}' is only {} bytes — smaller than an ELF64 header; "
            "not a loadable binary",
            rv_pdklib::rv_log_escape(info_entry), size);
        return RV_ERR_INVAL;
    }

    if (memcmp(mppcdisc_ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        RV_LOG_ERR(
            "pcloader",
            "code entry '{}' does not start with the ELF magic; it is not an "
            "ELF object at all",
            rv_pdklib::rv_log_escape(info_entry));
        return RV_ERR_INVAL;
    }

    // Single-byte fields, so they are readable regardless of byte order — and
    // only their verdict makes the multi-byte fields of the struct meaningful:
    // everything was copied under a little-endian assumption.
    if (mppcdisc_ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        mppcdisc_ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' is not a 64-bit little-endian ELF (class {}, "
            "data {}); this console only runs ELF64 LE discs",
            rv_pdklib::rv_log_escape(info_entry), (int)mppcdisc_ehdr.e_ident[EI_CLASS],
            (int)mppcdisc_ehdr.e_ident[EI_DATA]);
        return RV_ERR_INVAL;
    }

    if (mppcdisc_ehdr.e_type != ET_DYN || mppcdisc_ehdr.e_machine != EM_X86_64) {
        RV_LOG_ERR("pcloader",
            "code entry '{}' is not an x86-64 shared object (e_type {}, "
            "e_machine {}); it cannot run on this console",
            rv_pdklib::rv_log_escape(info_entry), mppcdisc_ehdr.e_type,
            mppcdisc_ehdr.e_machine);
        return RV_ERR_INVAL;
    }

    // The program-header walk steps by e_phentsize; if the file declares a
    // different stride than the Elf64_Phdr we read with, every entry after the
    // first would be read misaligned with the table.
    if (mppcdisc_ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        RV_LOG_ERR(
            "pcloader",
            "code entry '{}' declares {}-byte program headers, elf(5) says {}; "
            "its segment table cannot be walked",
            rv_pdklib::rv_log_escape(info_entry), mppcdisc_ehdr.e_phentsize,
            sizeof(Elf64_Phdr));
        return RV_ERR_INVAL;
    }

    bool version_info_found_flag = false;
    Elf64_Phdr potential_note;
    rv_mppc_note_desc version_info;
    for (int i = 0; i < mppcdisc_ehdr.e_phnum && !version_info_found_flag; ++i) {
        if (!pod_peek(buffer, mppcdisc_ehdr.e_phoff + i * mppcdisc_ehdr.e_phentsize,
                (mppcdisc_ehdr.e_phoff + i * mppcdisc_ehdr.e_phentsize) +
                    sizeof(potential_note),
                potential_note)) {
            RV_LOG_WARN("pcloader", "can not to peek elf64_phdr from disc.so ");
            continue;
        }

        if (potential_note.p_type == PT_NOTE) {
            const uint64_t segment_offset =
                static_cast<uint64_t>(potential_note.p_offset);
            const uint64_t segment_size =
                static_cast<uint64_t>(potential_note.p_filesz);
            const uint64_t buffer_size = static_cast<uint64_t>(buffer.size());

            if (segment_offset > buffer_size ||
                segment_size > buffer_size - segment_offset) {
                RV_LOG_ERR("pcloader", "ELF note segment exceeds mppcdisc bounds");
                return RV_ERR_INVAL;
            }

            const uint64_t segment_end = segment_offset + segment_size;

            constexpr uint64_t expected_owner_size =
                sizeof(RV_MPPC_NOTE_OWNER);
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
                    RV_LOG_WARN("pcloader",
                        "ELF note header exceeds note segment bounds");
                    break;
                }

                const uint64_t note_header_end = note_offset + sizeof(note);

                if (!pod_peek(buffer, note_offset, note_header_end, note)) {
                    RV_LOG_ERR("pcloader", "cannot read ELF note header from mppcdisc");
                    return RV_ERR_INVAL;
                }

                const uint64_t owner_size = static_cast<uint64_t>(note.n_namesz);
                const uint64_t desc_size = static_cast<uint64_t>(note.n_descsz);

                const uint64_t aligned_owner_size =
                    align_elf_note_field_size(owner_size);
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

                if (std::memcmp(owner, RV_MPPC_NOTE_OWNER,
                        expected_owner_size) != 0) {
                    continue;
                }

                if (!pod_peek(buffer, desc_offset, note_end, version_info)) {
                    RV_LOG_WARN("pcloader",
                        "cannot read version descriptor from ELF note");
                    continue;
                }

                version_info_found_flag = true;
                break;
            }
        }
    }

    if (!version_info_found_flag) {
        RV_LOG_ERR("pcloader", "version did not found in mppcdisc");
        return RV_ERR_INVAL;
    }

    // The disc code checksum: recomputed over the very buffer the ELF above
    // was parsed from, and compared against what the burner stamped into the
    // note. A mismatch means the code was altered after burning — refuse it
    // before dlopen ever sees the file.
    unsigned char computed_checksum[rv_pdklib::RV_DISC_HASH_BYTES];
    std::string hash_error;
    if (!rv_pdklib::rv_disc_hash_compute(buffer.data(), buffer.size(),
            computed_checksum, hash_error)) {
        RV_LOG_ERR("pcloader",
            "cannot checksum code entry '{}': {}",
            rv_pdklib::rv_log_escape(info_entry), hash_error);
        return RV_ERR_INVAL;
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
        return RV_ERR_INVAL;
    }

    if (RV_MPPC_VER_MAJOR != version_info.version_major ||
        RV_MPPC_VER_MINOR < version_info.version_minor) {
        RV_LOG_ERR("pcloader",
            "disc version are incompatible to currect console version: "
            "disc version is: {}.{}; ",
            version_info.version_major, version_info.version_minor);
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

} // namespace rv_3dmppc
