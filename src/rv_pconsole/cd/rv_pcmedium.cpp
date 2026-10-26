#include "rv_pconsole/cd/rv_pcmedium.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// std::filesystem::file_size answers in uintmax_t; the contract answers in a
// signed int64 whose negative half is reserved for rv_err. An entry too large to
// express is not a size we may return.
constexpr uintmax_t RV_PCMEDIUM_MAX_ENTRY_SIZE = static_cast<uintmax_t>(std::numeric_limits<int64_t>::max());

} // namespace

// SECURITY: the gate that keeps a resource name from becoming a path. See the
// long comment on the declaration in rv_pcmedium.hpp - everything refused here
// is a way of naming bytes that are not on the inserted disc.
bool rv_pcresname_valid(const char *resname)
{
    if (resname == nullptr) {
        return false;
    }
    if (resname[0] == '\0') {
        return false;
    }

    for (const char *p = resname; *p != '\0'; ++p) {
        // Directory separators: both host conventions, always.
        if (*p == '/' || *p == '\\') {
            return false;
        }
        // Drive letters and NTFS alternate data streams ("name:stream").
        if (*p == ':') {
            return false;
        }
        // Any run of two dots: `..`, `../x`, `a/../b` - the parent-directory
        // escape in every shape it takes.
        if (*p == '.' && *(p + 1) == '.') {
            return false;
        }
    }

    // A lone "." is the directory itself, not an entry in it.
    if (std::strcmp(resname, ".") == 0) {
        return false;
    }

    return true;
}

rv_pcdirmedium::rv_pcdirmedium(const std::string &dir_path)
{
    if (dir_path.empty()) {
        // No disc in the drive. Deliberately quiet: this is how a smoke run
        // with cv=null of the console with no game boots, and it is not a fault.
        RV_LOG_INFO("pcmedium", "no medium path configured; drive is empty");
        return;
    }

    std::error_code ec;
    std::filesystem::path root = std::filesystem::path(dir_path).lexically_normal();
    if (!std::filesystem::is_directory(root, ec)) {
        // Worth shouting about - someone pointed the console at a disc that is
        // not there - but the machine stays usable, just empty.
        RV_LOG_ERR("pcmedium", "medium path '{}' is not a directory; drive stays empty", dir_path);
        return;
    }

    root_ = std::move(root);
    mounted_ = true;
    RV_LOG_INFO("pcmedium", "mounted directory medium '{}'", root_.string());
}

int rv_pcdirmedium::entry_path(const char *resname, std::filesystem::path &out) const
{
    if (!mounted_ || !rv_pcresname_valid(resname)) {
        return RV_ERR_NOENT;
    }

    std::filesystem::path candidate = (root_ / resname).lexically_normal();

    // Defence in depth: whatever the name did to the path, the result must still
    // be a DIRECT child of the medium root. A validated name can never fail this
    // - which is exactly why a failure here means the validator was bypassed.
    if (candidate.parent_path() != root_) {
        RV_LOG_ERR("pcmedium", "entry name '{}' escapes the medium root; refused", resname);
        return RV_ERR_NOENT;
    }

    out = std::move(candidate);
    return RV_OK;
}

int64_t rv_pcdirmedium::entry_size(const char *resname) const
{
    std::filesystem::path path;
    if (entry_path(resname, path) != RV_OK) {
        return RV_ERR_NOENT;
    }

    std::error_code ec;
    // A directory, a device node or a dangling symlink is not an entry: the
    // medium holds files and nothing else, so anything else simply is not there.
    if (!std::filesystem::is_regular_file(path, ec)) {
        return RV_ERR_NOENT;
    }

    uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        // It exists and is a file, yet cannot be measured: the device failed a
        // well-formed request, which is RV_ERR_IO and not RV_ERR_NOENT.
        RV_LOG_ERR("pcmedium", "cannot measure entry '{}': {}", resname, ec.message());
        return RV_ERR_IO;
    }
    if (size > RV_PCMEDIUM_MAX_ENTRY_SIZE) {
        RV_LOG_ERR("pcmedium", "entry '{}' is too large to address", resname);
        return RV_ERR_IO;
    }

    return static_cast<int64_t>(size);
}

int64_t rv_pcdirmedium::entry_read(const char *resname, void *baddr, int64_t cap) const
{
    if (baddr == nullptr || cap < 0) {
        return RV_ERR_INVAL;
    }

    std::filesystem::path path;
    if (entry_path(resname, path) != RV_OK) {
        return RV_ERR_NOENT;
    }

    // Measure first, and refuse a short buffer BEFORE opening anything: an
    // undersized read must cost the game nothing and leave its buffer untouched.
    const int64_t size = entry_size(resname);
    if (size < 0) {
        return size;
    }
    if (cap < size) {
        RV_LOG_WARN("pcmedium", "entry '{}' needs {} bytes, buffer holds {}", resname, size, cap);
        return RV_ERR_INVAL;
    }

    // An empty entry is a legal entry: nothing to transfer, zero bytes written.
    if (size == 0) {
        return 0;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        RV_LOG_ERR("pcmedium", "cannot open entry '{}' for reading", resname);
        return RV_ERR_IO;
    }

    in.read(static_cast<char *>(baddr), static_cast<std::streamsize>(size));
    const std::streamsize got = in.gcount();
    if (got != static_cast<std::streamsize>(size)) {
        // The contract promises nothing is written to `baddr` on failure. Every
        // failure that CAN be detected before the transfer already has been
        // (missing entry, short buffer, unopenable file); the only window left is
        // the medium dying mid-copy, and the bytes are then already in the game's
        // buffer. Scrubbing what we touched is the closest honest equivalent:
        // the game cannot mistake half an entry for a whole one. The alternative
        // - staging the entry in a shadow allocation - would make the drive
        // allocate per read, which rv_cd.hpp explicitly forbids.
        std::memset(baddr, 0, static_cast<size_t>(size));
        RV_LOG_ERR("pcmedium", "short read on entry '{}': {} of {} bytes", resname, static_cast<int64_t>(got), size);
        return RV_ERR_IO;
    }

    return size;
}

int64_t rv_pcdirmedium::entry_read_at(const char *resname, int64_t offset, void *dst, int64_t count) const
{
    if ((dst == nullptr && count > 0) || offset < 0 || count < 0) {
        RV_LOG_WARN("pcmedium", "bad range arguments: offset {}, count {}", offset, count);
        return RV_ERR_INVAL;
    }

    std::filesystem::path path;
    if (entry_path(resname, path) != RV_OK) {
        RV_LOG_WARN("pcmedium", "no entry '{}' on the medium", rv_pdklib::rv_log_escape(resname));
        return RV_ERR_NOENT;
    }

    // Range is checked against the measured size before any byte moves. Written as
    // `count > size - offset` so the sum can never overflow int64.
    const int64_t size = entry_size(resname);
    if (size < 0) {
        // entry_size logs its IO failures itself; a non-regular file is NOENT and silent there.
        if (size == RV_ERR_NOENT) {
            RV_LOG_WARN("pcmedium", "entry '{}' is not a regular file", rv_pdklib::rv_log_escape(resname));
        }
        return size;
    }
    if (offset > size || count > size - offset) {
        RV_LOG_WARN("pcmedium",
            "range {}+{} is outside entry '{}' of {} bytes",
            offset,
            count,
            rv_pdklib::rv_log_escape(resname),
            size);
        return RV_ERR_INVAL;
    }

    // Nothing to transfer: no descriptor is opened for a zero-length portion.
    if (count == 0) {
        return 0;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        RV_LOG_ERR("pcmedium", "cannot open entry '{}' for reading", rv_pdklib::rv_log_escape(resname));
        return RV_ERR_IO;
    }

    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) {
        RV_LOG_ERR("pcmedium", "cannot seek entry '{}' to byte {}", rv_pdklib::rv_log_escape(resname), offset);
        return RV_ERR_IO;
    }

    in.read(static_cast<char *>(dst), static_cast<std::streamsize>(count));
    const std::streamsize got = in.gcount();
    if (got != static_cast<std::streamsize>(count)) {
        // Same scrub as entry_read: the game must not mistake a partial portion for a whole one.
        std::memset(dst, 0, static_cast<size_t>(count));
        RV_LOG_ERR("pcmedium",
            "short read on '{}' at {}: {} of {} bytes",
            rv_pdklib::rv_log_escape(resname),
            offset,
            static_cast<int64_t>(got),
            count);
        return RV_ERR_IO;
    }

    return count;
}

} // namespace rv_3dmppc
