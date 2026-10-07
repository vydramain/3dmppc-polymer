// The medium behind rv_cm: one file holding every slot of one memory card.
//
// The card is small enough (16 x 8 KiB = 128 KiB on the reference machine) that
// the whole image is read into RAM once at construction and lives there for the
// session; the file exists only so saves outlive the process. Reads never touch
// the disk, and a write rewrites the entire image - which is what makes the
// contract's atomicity promise implementable (see the note in rv_pccard.cpp).
//
// Operations that can fail return rv_err codes; yes/no questions (medium_ok(), valid())
// stay bool. rv_pccm maps error codes to the public contract vocabulary.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

// On-disk layout, little-endian throughout, in this order:
//
//   header  : magic[8] "MPPCCARD", u32 version, u32 reserved,
//             i64 slot_count, i64 slot_size                    (32 bytes)
//   lengths : i64 x slot_count - stored bytes per slot, -1 = empty slot
//   payload : slot_size x slot_count, fixed stride, unused tail is zeroed
//
// Fixed stride costs nothing worth saving (the card is tiny) and buys a
// property that matters: the file size is a pure function of the geometry, so a
// truncated image is detected by a size comparison alone.
class rv_pccard
{
public:
    // A card is hardware, not a filesystem: a geometry that would demand a
    // multi-megabyte image is a misconfiguration. Public so the boot budget
    // check (rv_pccm_posix::evaluate) can refuse the same geometry before any
    // disc code loads, instead of leaving this constructor to log the refusal
    // after the disc is already running - the two must never drift apart.
    static constexpr int64_t RV_PCCARD_MAX_IMAGE_BYTES = 64 * 1024 * 1024;

    // The on-disk header (magic[8] + version u32 + reserved u32 + slot_count
    // i64 + slot_size i64) and one length entry (i64) per slot, ahead of the
    // slot payloads. Public so the boot budget check
    // (rv_pccm_posix::evaluate) can cost a card image before one is built.
    static constexpr int64_t RV_PCCARD_HEADER_BYTES = 8 /* magic */ + sizeof(uint32_t) /* version */ +
        sizeof(uint32_t) /* reserved */ + sizeof(int64_t) /* slot_count */ + sizeof(int64_t) /* slot_size */;
    static constexpr int64_t RV_PCCARD_LENGTH_ENTRY_BYTES = sizeof(int64_t);

    // Loads `image_path`; boot always passes one (rv_pboot_conf.cpp).
    // A missing file is NOT an error: the card simply reads as all-empty and
    // the file is created by the first successful write. A file that exists but
    // does not match the requested geometry - or is corrupt - leaves the card
    // UNUSABLE rather than being reformatted: those bytes are somebody's saves.
    rv_pccard(const std::string &image_path, int64_t slot_count, int64_t slot_size);

    // False when the backing file could not be trusted. Every operation then
    // fails; nothing is ever written over the file that caused it.
    bool medium_ok() const
    {
        return medium_ok_;
    }

    // Did the card actually come up with the geometry it was asked for? A
    // missing image is not a failure (the card starts empty by design); a
    // rejected geometry or an untrustworthy image is.
    bool valid() const
    {
        return medium_ok_;
    }

    int64_t slot_count() const
    {
        return slot_count_;
    }
    int64_t slot_size() const
    {
        return slot_size_;
    }

    // Stored bytes in `slot`, or -1 when the slot is empty. `slot` must be in
    // range - range checking belongs to the controller.
    int64_t slot_length(int64_t slot) const;

    // First byte of `slot`'s payload; only slot_length() bytes are meaningful.
    const uint8_t *slot_data(int64_t slot) const;

    // Replace `slot` with `size` bytes of `data` and persist the image.
    // Returns RV_OK on success; RV_ERR_IO on a medium failure, and then the slot -
    // in memory and on disk alike - still holds exactly what it held before the call.
    int slot_write(int64_t slot, const void *data, int64_t size);

    // Empty `slot` and persist. Erasing an already-empty slot touches nothing
    // and succeeds, so it never brings a file into existence. Returns RV_OK or RV_ERR_IO.
    int slot_erase(int64_t slot);

private:
    // Reads the file into image_. Returns RV_OK on success, RV_ERR_IO on a read failure,
    // or RV_ERR_INVAL for a file that exists and cannot be trusted. A missing file yields
    // a freshly formatted RAM image and returns RV_OK.
    int load();

    // Read entire file of `want` bytes into `out`, or return error.
    int read_full(int64_t want, std::vector<uint8_t> &out, const std::filesystem::path &path);

    // Move old image aside and log the refusal.
    int set_aside(uint32_t old_version, const std::filesystem::path &path, std::string &aside_path);

    // Load a same-major older-minor compatible image (restamp to current version in RAM).
    int load_compatible_(uint32_t version,
        int64_t file_slots,
        int64_t file_slot_size,
        uintmax_t on_disk,
        int64_t expected,
        const std::filesystem::path &path);

    // Load an older-major image (migrate layout to current version, set old file aside, flush).
    int load_migrate_(uint32_t version,
        int64_t file_slots,
        int64_t file_slot_size,
        uintmax_t on_disk,
        const std::filesystem::path &path);

    // Handle an incompatible image (newer major or same major newer minor).
    int load_incompatible_(uint32_t version, const std::filesystem::path &path);

    // Load a current-version image (validate geometry and size, read and check slots).
    int load_current_(int64_t file_slots,
        int64_t file_slot_size,
        uintmax_t on_disk,
        int64_t expected,
        const std::filesystem::path &path);

    // Writes image_ out atomically (temp file + fsync + rename). Returns RV_OK or RV_ERR_IO.
    int flush();

    // Apply one slot mutation (`new_length` < 0 empties the slot) and persist
    // it, undoing the in-RAM change when the persist fails. The single place
    // where the contract's "old content intact" promise is kept. Returns RV_OK or RV_ERR_IO.
    int commit(int64_t slot, int64_t new_length, const void *data);

    void format_empty();
    int64_t length_at(int64_t slot) const;
    void set_length(int64_t slot, int64_t length);
    uint8_t *payload_at(int64_t slot);

    std::string image_path_;
    int64_t slot_count_ = 0;
    int64_t slot_size_ = 0;
    int64_t payload_offset_ = 0;
    bool medium_ok_ = true;
    std::vector<uint8_t> image_;
};

} // namespace rv_3dmppc
