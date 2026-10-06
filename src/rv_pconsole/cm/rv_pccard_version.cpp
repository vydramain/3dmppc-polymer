#include "rv_pccard_version.hpp"
#include "rv_pccard_bytes.hpp"

#include <algorithm>
#include <cstring>

#include "pdklib/rv_version/rv_version.hpp"

namespace rv_3dmppc
{
namespace
{

// Must match rv_pccard.cpp's on-disk layout: header[32] then one i64 length
// per slot, ahead of the payload. Fixed here because a container layout
// change only ever comes with a major bump, at which point this whole file
// gets rewritten for the new predecessor anyway.
constexpr int64_t RV_PCCARD_HEADER_SIZE = 32;
constexpr int64_t RV_PCCARD_LENGTH_ENTRY = 8;

using rv_pccard_detail::BITS_PER_BYTE;
using rv_pccard_detail::BYTE_MASK;
using rv_pccard_detail::BYTES_PER_I64;
using rv_pccard_detail::BYTES_PER_U32;
using rv_pccard_detail::get_i64;
using rv_pccard_detail::put_i64;
using rv_pccard_detail::put_u32;

// Version packing: major in bits[31:16], minor in bits[15:0]
constexpr int VERSION_MAJOR_SHIFT = 16;
constexpr uint32_t VERSION_MINOR_MASK = 0xFFFFu;

// Header layout: magic[8] version[4] reserved[4] slot_count[8] slot_size[8]
constexpr int HEADER_MAGIC_SIZE = 8;
constexpr int64_t HEADER_OFFSET_VERSION = HEADER_MAGIC_SIZE;
constexpr int64_t HEADER_OFFSET_RESERVED = HEADER_OFFSET_VERSION + BYTES_PER_U32;
constexpr int64_t HEADER_OFFSET_SLOT_COUNT = HEADER_OFFSET_RESERVED + BYTES_PER_U32;
constexpr int64_t HEADER_OFFSET_SLOT_SIZE = HEADER_OFFSET_SLOT_COUNT + BYTES_PER_I64;

} // namespace

std::string rv_pccard_version_text(uint32_t v)
{
    return std::to_string(v >> VERSION_MAJOR_SHIFT) + "." + std::to_string(v & VERSION_MINOR_MASK);
}

rv_pccard_version_case rv_pccard_classify_version(uint32_t file_version, uint32_t console_version)
{
    const uint32_t file_major = file_version >> VERSION_MAJOR_SHIFT;
    const uint32_t file_minor = file_version & VERSION_MINOR_MASK;
    const uint32_t console_major = console_version >> VERSION_MAJOR_SHIFT;

    // Callers only reach here when file_version != console_version, so a
    // compatible major/minor pair here means strictly older, never equal.
    if (rv_pdklib::rv_version_compatible(file_major, file_minor)) {
        return rv_pccard_version_case::compatible;
    }
    if (file_major < console_major) {
        return rv_pccard_version_case::migrate;
    }
    return rv_pccard_version_case::incompatible;
}

std::vector<uint8_t> rv_pccard_migrate(const std::vector<uint8_t> &old_buffer, int64_t file_slots,
    int64_t file_slot_size, int64_t old_payload_offset, int64_t slot_count, int64_t slot_size,
    int64_t payload_offset, uint32_t console_version, std::string &error, std::vector<std::string> &warnings)
{
    error.clear();
    for (int64_t i = 0; i < file_slots; ++i) {
        const int64_t length = get_i64(old_buffer.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY);
        if (length < -1 || length > file_slot_size) {
            error = "slot " + std::to_string(i) + " claims " + std::to_string(length) + " byte(s) - corrupt";
            return {};
        }
    }

    std::vector<uint8_t> image(static_cast<size_t>(payload_offset + slot_count * slot_size), 0);
    static constexpr uint8_t magic[HEADER_MAGIC_SIZE] = { 'M', 'P', 'P', 'C', 'C', 'A', 'R', 'D' };
    std::memcpy(image.data(), magic, sizeof(magic));
    put_u32(image.data() + HEADER_OFFSET_VERSION, console_version);
    put_u32(image.data() + HEADER_OFFSET_RESERVED, 0);
    put_i64(image.data() + HEADER_OFFSET_SLOT_COUNT, slot_count);
    put_i64(image.data() + HEADER_OFFSET_SLOT_SIZE, slot_size);
    for (int64_t i = 0; i < slot_count; ++i) {
        put_i64(image.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY, -1);
    }

    const int64_t common_slots = std::min(file_slots, slot_count);
    for (int64_t i = 0; i < common_slots; ++i) {
        const int64_t length = get_i64(old_buffer.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY);
        if (length <= 0) {
            continue;
        }
        if (length > slot_size) {
            warnings.push_back("slot " + std::to_string(i) + " has " + std::to_string(length) +
                " byte(s), does not fit the new " + std::to_string(slot_size) + " byte slot - dropped");
            continue;
        }
        const uint8_t *old_payload = old_buffer.data() + old_payload_offset + i * file_slot_size;
        std::memcpy(image.data() + payload_offset + i * slot_size, old_payload, static_cast<size_t>(length));
        put_i64(image.data() + RV_PCCARD_HEADER_SIZE + i * RV_PCCARD_LENGTH_ENTRY, length);
    }
    if (file_slots > slot_count) {
        warnings.push_back("held " + std::to_string(file_slots) + " slot(s), this console has " +
            std::to_string(slot_count) + " - " + std::to_string(file_slots - slot_count) +
            " slot(s) beyond that dropped");
    }

    return image;
}

} // namespace rv_3dmppc
