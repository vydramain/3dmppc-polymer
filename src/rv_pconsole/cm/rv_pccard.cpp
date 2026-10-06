#include "rv_pccard.hpp"
#include "rv_pccard_version.hpp"
#include "rv_pccard_bytes.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_3dmppc
{
namespace
{

constexpr const char *RV_PCCARD_TAG = "pccard";

constexpr int64_t RV_PCCARD_HEADER_SIZE = 32;
constexpr int64_t RV_PCCARD_LENGTH_ENTRY = 8;
constexpr uint32_t RV_PCCARD_VERSION = rv_pdklib::rv_version_packed32;

constexpr uint8_t RV_PCCARD_MAGIC[8] = { 'M', 'P', 'P', 'C', 'C', 'A', 'R', 'D' };

using rv_pccard_detail::get_i64;
using rv_pccard_detail::get_u32;
using rv_pccard_detail::put_i64;
using rv_pccard_detail::put_u32;

} // namespace

rv_pccard::rv_pccard(const std::string &image_path, int64_t slot_count, int64_t slot_size)
    : image_path_(image_path)
    , slot_count_(slot_count)
    , slot_size_(slot_size)
{
    if (slot_count_ <= 0 || slot_size_ <= 0) {
        RV_LOG_ERR(RV_PCCARD_TAG, "refusing geometry {} slot(s) of {} byte(s)", slot_count_, slot_size_);
        medium_ok_ = false;
        slot_count_ = 0;
        slot_size_ = 0;
        return;
    }
    if (slot_count_ > RV_PCCARD_MAX_IMAGE_BYTES / RV_PCCARD_LENGTH_ENTRY ||
        slot_size_ > (RV_PCCARD_MAX_IMAGE_BYTES - RV_PCCARD_HEADER_SIZE) / slot_count_) {
        RV_LOG_ERR(RV_PCCARD_TAG, "geometry {}x{} exceeds the {} byte image limit", slot_count_, slot_size_,
            RV_PCCARD_MAX_IMAGE_BYTES);
        medium_ok_ = false;
        return;
    }

    payload_offset_ = RV_PCCARD_HEADER_SIZE + slot_count_ * RV_PCCARD_LENGTH_ENTRY;
    medium_ok_ = load();
}

void rv_pccard::format_empty()
{
    image_.assign(static_cast<size_t>(payload_offset_ + slot_count_ * slot_size_), 0);
    std::memcpy(image_.data(), RV_PCCARD_MAGIC, sizeof(RV_PCCARD_MAGIC));
    put_u32(image_.data() + 8, RV_PCCARD_VERSION);
    put_u32(image_.data() + 12, 0);
    put_i64(image_.data() + 16, slot_count_);
    put_i64(image_.data() + 24, slot_size_);
    for (int64_t i = 0; i < slot_count_; ++i) {
        set_length(i, -1);
    }
}

bool rv_pccard::load()
{
    const int64_t expected = payload_offset_ + slot_count_ * slot_size_;
    const std::filesystem::path path(image_path_);

    std::error_code ec;
    const bool present = std::filesystem::exists(path, ec);
    if (ec) {
        RV_LOG_ERR(RV_PCCARD_TAG, "cannot stat image '{}': {}", image_path_, ec.message());
        return false;
    }
    if (!present) {
        // Lazy creation: an absent image is a brand-new card, not a fault. The
        // file appears the first time a disc actually saves something, so a
        // console that is only ever run never leaves a file behind.
        format_empty();
        RV_LOG_INFO(RV_PCCARD_TAG, "no image at '{}', card starts empty ({} slot(s) of {} byte(s))",
            image_path_, slot_count_, slot_size_);
        return true;
    }

    const auto on_disk = std::filesystem::file_size(path, ec);
    if (ec) {
        RV_LOG_ERR(RV_PCCARD_TAG, "cannot size image '{}': {}", image_path_, ec.message());
        return false;
    }
    // The version decides how the rest of the file is read, and an older major
    // can carry a different slot geometry than this console's - so the size is
    // checked against the header's OWN claimed geometry, not this console's,
    // before any version decision. Only the current-version path below re-checks
    // size against this console's geometry.
    if (on_disk < static_cast<uintmax_t>(RV_PCCARD_HEADER_SIZE)) {
        RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' is {} byte(s), too small for a header - refusing to touch it",
            image_path_, static_cast<int64_t>(on_disk));
        return false;
    }

    auto read_full = [&](int64_t want, std::vector<uint8_t> &out) -> bool {
        out.assign(static_cast<size_t>(want), 0);
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            RV_LOG_ERR(RV_PCCARD_TAG, "cannot open image '{}' for reading", image_path_);
            return false;
        }
        in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(want));
        if (in.gcount() != static_cast<std::streamsize>(want)) {
            RV_LOG_ERR(RV_PCCARD_TAG, "short read on image '{}'", image_path_);
            return false;
        }
        return true;
    };

    // Moves the file aside as "<image>.<M.m>" of `old_version`. Shared by the
    // "cannot know this layout" refusal and the older-major migration below.
    auto set_aside = [&](uint32_t old_version, std::string &aside_path) -> bool {
        aside_path = image_path_ + "." + rv_pccard_version_text(old_version);
        std::error_code aside_ec;
        if (std::filesystem::exists(aside_path, aside_ec) || aside_ec) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' has version {}, this console speaks {} - refusing "
                "('{}' already exists)", image_path_, rv_pccard_version_text(old_version),
                rv_pccard_version_text(RV_PCCARD_VERSION), aside_path);
            return false;
        }
        std::filesystem::rename(path, aside_path, aside_ec);
        if (aside_ec) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' has version {}, this console speaks {} - refusing "
                "(could not set it aside as '{}': {})", image_path_, rv_pccard_version_text(old_version),
                rv_pccard_version_text(RV_PCCARD_VERSION), aside_path, aside_ec.message());
            return false;
        }
        return true;
    };

    std::vector<uint8_t> header;
    if (!read_full(RV_PCCARD_HEADER_SIZE, header)) {
        return false;
    }
    if (std::memcmp(header.data(), RV_PCCARD_MAGIC, sizeof(RV_PCCARD_MAGIC)) != 0) {
        RV_LOG_ERR(RV_PCCARD_TAG, "'{}' is not a card image (bad magic)", image_path_);
        return false;
    }
    const uint32_t version = get_u32(header.data() + 8);
    const int64_t file_slots = get_i64(header.data() + 16);
    const int64_t file_slot_size = get_i64(header.data() + 24);

    if (version != RV_PCCARD_VERSION && rv_pccard_classify_version(version, RV_PCCARD_VERSION) ==
        rv_pccard_version_case::compatible) {
        // Same major, older minor: this console's layout can still read it. The
        // header is restamped to this console's version in RAM; it only reaches
        // disk on the next actual write.
        if (file_slots != slot_count_ || file_slot_size != slot_size_) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' holds {}x{} byte slots, this console has {}x{} - refusing",
                image_path_, file_slots, file_slot_size, slot_count_, slot_size_);
            return false;
        }
        if (on_disk != static_cast<uintmax_t>(expected)) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' is {} byte(s), expected {} - refusing to touch it",
                image_path_, static_cast<int64_t>(on_disk), expected);
            return false;
        }
        std::vector<uint8_t> buffer;
        if (!read_full(expected, buffer)) {
            return false;
        }
        for (int64_t i = 0; i < slot_count_; ++i) {
            const int64_t length = get_i64(buffer.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY);
            if (length < -1 || length > slot_size_) {
                RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' slot {} claims {} byte(s) - corrupt", image_path_, i, length);
                return false;
            }
        }
        put_u32(buffer.data() + 8, RV_PCCARD_VERSION);
        image_ = std::move(buffer);
        RV_LOG_INFO(RV_PCCARD_TAG, "card image '{}' was version {}, restamped to {} ({} slot(s) of {} byte(s))",
            image_path_, rv_pccard_version_text(version), rv_pccard_version_text(RV_PCCARD_VERSION), slot_count_, slot_size_);
        return true;
    }

    if (version != RV_PCCARD_VERSION && rv_pccard_classify_version(version, RV_PCCARD_VERSION) ==
        rv_pccard_version_case::migrate) {
        // Older major: sanity-check the old header's own geometry before
        // trusting it for a read size (see rv_pccard_version.cpp for the
        // migration itself).
        if (file_slots <= 0 || file_slot_size <= 0 ||
            file_slots > RV_PCCARD_MAX_IMAGE_BYTES / RV_PCCARD_LENGTH_ENTRY ||
            file_slot_size > (RV_PCCARD_MAX_IMAGE_BYTES - RV_PCCARD_HEADER_SIZE) / file_slots) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' claims {}x{} byte slots - corrupt, refusing", image_path_,
                file_slots, file_slot_size);
            return false;
        }
        const int64_t old_payload_offset = RV_PCCARD_HEADER_SIZE + file_slots * RV_PCCARD_LENGTH_ENTRY;
        const int64_t old_expected = old_payload_offset + file_slots * file_slot_size;
        if (on_disk != static_cast<uintmax_t>(old_expected)) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' is {} byte(s), expected {} for its own {}x{} geometry - "
                "corrupt, refusing", image_path_, static_cast<int64_t>(on_disk), old_expected, file_slots,
                file_slot_size);
            return false;
        }
        std::vector<uint8_t> old_buffer;
        if (!read_full(old_expected, old_buffer)) {
            return false;
        }

        std::string migrate_error;
        std::vector<std::string> dropped;
        std::vector<uint8_t> migrated = rv_pccard_migrate(old_buffer, file_slots, file_slot_size,
            old_payload_offset, slot_count_, slot_size_, payload_offset_, RV_PCCARD_VERSION, migrate_error,
            dropped);
        if (!migrate_error.empty()) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' {} - refusing", image_path_, migrate_error);
            return false;
        }

        std::string aside_path;
        if (!set_aside(version, aside_path)) {
            return false;
        }
        image_ = std::move(migrated);
        if (!flush()) {
            // Aside already happened; a crash between here and the next save
            // must not leave the card missing entirely, only unwritten - retry
            // the write, do not fall back to refusing after the old file is gone.
            RV_LOG_ERR(RV_PCCARD_TAG, "migrated '{}' but could not write the new image - retry the save",
                image_path_);
            return false;
        }
        for (const auto &line : dropped) {
            RV_LOG_WARN(RV_PCCARD_TAG, "migrating '{}': {}", aside_path, line);
        }
        RV_LOG_INFO(RV_PCCARD_TAG, "migrated '{}' from version {} to {}: {} slot(s) of {} byte(s), {} dropped, "
            "old image kept as '{}'", image_path_, rv_pccard_version_text(version), rv_pccard_version_text(RV_PCCARD_VERSION),
            slot_count_, slot_size_, dropped.size(), aside_path);
        return true;
    }

    if (version != RV_PCCARD_VERSION) {
        // Newer major, or same major with a newer minor: this console cannot
        // know that layout. Set the old file aside by name and start empty
        // rather than refuse to boot; a failed rename keeps the refusal so
        // nothing is silently overwritten.
        std::string aside_path;
        if (!set_aside(version, aside_path)) {
            return false;
        }
        RV_LOG_INFO(RV_PCCARD_TAG, "image '{}' has version {}, this console speaks {} - set aside as '{}', "
            "card starts empty", image_path_, rv_pccard_version_text(version),
            rv_pccard_version_text(RV_PCCARD_VERSION), aside_path);
        format_empty();
        return true;
    }

    if (on_disk != static_cast<uintmax_t>(expected)) {
        // Truncated, padded, or written by a console with a different geometry.
        // Either way these bytes are somebody's saves: refuse the medium rather
        // than reformat it. A card that answers RV_ERR_IO is recoverable by
        // moving the file aside; one that was silently reformatted is not.
        RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' is {} byte(s), expected {} - refusing to touch it",
            image_path_, static_cast<int64_t>(on_disk), expected);
        return false;
    }
    if (file_slots != slot_count_ || file_slot_size != slot_size_) {
        RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' holds {}x{} byte slots, this console has {}x{} - refusing",
            image_path_, file_slots, file_slot_size, slot_count_, slot_size_);
        return false;
    }

    std::vector<uint8_t> buffer;
    if (!read_full(expected, buffer)) {
        return false;
    }
    for (int64_t i = 0; i < slot_count_; ++i) {
        const int64_t length = get_i64(buffer.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY);
        if (length < -1 || length > slot_size_) {
            RV_LOG_ERR(RV_PCCARD_TAG, "image '{}' slot {} claims {} byte(s) - corrupt", image_path_, i,
                length);
            return false;
        }
    }

    image_ = std::move(buffer);
    RV_LOG_INFO(RV_PCCARD_TAG, "card image '{}' loaded ({} slot(s) of {} byte(s))", image_path_, slot_count_,
        slot_size_);
    return true;
}

} // namespace rv_3dmppc
