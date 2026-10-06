// Slot access and mutation for rv_pccard: write, erase, and commit operations.
#include "rv_pccard.hpp"
#include "rv_pccard_bytes.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{
namespace
{

constexpr const char *RV_PCCARD_TAG = "pccard";

constexpr int64_t RV_PCCARD_HEADER_SIZE = 32;
constexpr int64_t RV_PCCARD_LENGTH_ENTRY = 8;

using rv_pccard_detail::put_i64;
using rv_pccard_detail::get_i64;

std::string errno_text(int e)
{
    return std::string(std::strerror(e));
}

} // namespace

int64_t rv_pccard::length_at(int64_t slot) const
{
    return get_i64(image_.data() + RV_PCCARD_HEADER_SIZE + slot * RV_PCCARD_LENGTH_ENTRY);
}

void rv_pccard::set_length(int64_t slot, int64_t length)
{
    put_i64(image_.data() + RV_PCCARD_HEADER_SIZE + slot * RV_PCCARD_LENGTH_ENTRY, length);
}

uint8_t *rv_pccard::payload_at(int64_t slot)
{
    return image_.data() + payload_offset_ + slot * slot_size_;
}

int64_t rv_pccard::slot_length(int64_t slot) const
{
    if (!medium_ok_) {
        return -1;
    }
    return length_at(slot);
}

const uint8_t *rv_pccard::slot_data(int64_t slot) const
{
    return image_.data() + payload_offset_ + slot * slot_size_;
}

bool rv_pccard::slot_write(int64_t slot, const void *data, int64_t size)
{
    if (!medium_ok_) {
        return false;
    }
    return commit(slot, size, data);
}

bool rv_pccard::slot_erase(int64_t slot)
{
    if (!medium_ok_) {
        return false;
    }
    if (length_at(slot) < 0) {
        // Already empty. Skipping the flush keeps erase from creating an image
        // file for a card that has never held a save.
        return true;
    }
    return commit(slot, -1, nullptr);
}

bool rv_pccard::commit(int64_t slot, int64_t new_length, const void *data)
{
    const int64_t old_length = length_at(slot);
    uint8_t *payload = payload_at(slot);

    // The undo log: the bytes that must reappear if the medium refuses the
    // write. Only the meaningful prefix is kept - the invariant that everything
    // past a slot's length is zero makes the rest reconstructible.
    std::vector<uint8_t> undo;
    if (old_length > 0) {
        undo.assign(payload, payload + old_length);
    }

    std::memset(payload, 0, static_cast<size_t>(slot_size_));
    if (new_length > 0) {
        std::memcpy(payload, data, static_cast<size_t>(new_length));
    }
    set_length(slot, new_length);

    if (flush()) {
        return true;
    }

    std::memset(payload, 0, static_cast<size_t>(slot_size_));
    if (old_length > 0) {
        std::memcpy(payload, undo.data(), static_cast<size_t>(old_length));
    }
    set_length(slot, old_length);
    return false;
}

bool rv_pccard::flush()
{
    // Atomic replace via rename - POSIX requires rename(2) to be
    // atomic WITHIN ONE FILESYSTEM: any observer, including the next boot after
    // a power cut, sees either the old inode whole or the new inode whole, and
    // never a state in between. The entire image is therefore built in RAM,
    // written to a fresh temporary file, forced to the platter with fsync, and
    // only then renamed over the target. That, and not "careful in-place
    // writing", is what discharges rv_cm's promise that a failed card_write
    // leaves the previous content intact: an in-place write of 128 KiB is many
    // device operations, and a crash in the middle of them leaves a slot half
    // old and half new - exactly the state the contract forbids.
    //
    // Two conditions are load-bearing and easy to lose in a refactor:
    //   * the temporary MUST live in the same directory as the target. A temp
    //     in /tmp can land on a different filesystem, where rename fails
    //     outright (EXDEV) or gets "helpfully" replaced by copy-then-unlink,
    //     which is not atomic at all.
    //   * fsync of the FILE orders the data before the rename; fsync of the
    //     DIRECTORY is what makes the rename itself durable. Without the second
    //     one a crash can resurrect the old name pointing at nothing.
    const std::filesystem::path target(image_path_);
    std::filesystem::path dir = target.parent_path();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            RV_LOG_ERR(RV_PCCARD_TAG, "cannot create '{}': {}", dir.string(), ec.message());
            return false;
        }
    } else {
        dir = std::filesystem::path(".");
    }

    const std::string tmp_path =
        image_path_ + ".tmp" + std::to_string(static_cast<long long>(::getpid()));

    const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        RV_LOG_ERR(RV_PCCARD_TAG, "cannot create '{}': {}", tmp_path, errno_text(errno));
        return false;
    }

    bool ok = true;
    size_t written = 0;
    while (written < image_.size()) {
        const ssize_t n = ::write(fd, image_.data() + written, image_.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            RV_LOG_ERR(RV_PCCARD_TAG, "write to '{}' failed: {}", tmp_path, errno_text(errno));
            ok = false;
            break;
        }
        written += static_cast<size_t>(n);
    }

    if (ok && ::fsync(fd) != 0) {
        RV_LOG_ERR(RV_PCCARD_TAG, "fsync of '{}' failed: {}", tmp_path, errno_text(errno));
        ok = false;
    }
    if (::close(fd) != 0 && ok) {
        RV_LOG_ERR(RV_PCCARD_TAG, "close of '{}' failed: {}", tmp_path, errno_text(errno));
        ok = false;
    }

    if (ok && ::rename(tmp_path.c_str(), image_path_.c_str()) != 0) {
        RV_LOG_ERR(RV_PCCARD_TAG, "rename '{}' -> '{}' failed: {}", tmp_path, image_path_,
            errno_text(errno));
        ok = false;
    }

    if (!ok) {
        ::unlink(tmp_path.c_str());
        return false;
    }

    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        // Best effort: the rename already happened and the card is consistent
        // either way, so a directory that cannot be synced is a durability
        // remark, not a failed save.
        if (::fsync(dir_fd) != 0) {
            RV_LOG_WARN(RV_PCCARD_TAG, "fsync of '{}' failed: {}", dir.string(), errno_text(errno));
        }
        ::close(dir_fd);
    } else {
        RV_LOG_WARN(RV_PCCARD_TAG, "cannot open '{}' to sync: {}", dir.string(), errno_text(errno));
    }
    return true;
}

} // namespace rv_3dmppc
