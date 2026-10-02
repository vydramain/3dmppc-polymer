#include "rv_pccard_version.hpp"

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

void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

void put_i64(uint8_t *p, int64_t v)
{
    const uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((u >> (8 * i)) & 0xFFu);
    }
}

int64_t get_i64(const uint8_t *p)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return static_cast<int64_t>(u);
}

} // namespace

std::string rv_pccard_version_text(uint32_t v)
{
    return std::to_string(v >> 16) + "." + std::to_string(v & 0xFFFFu);
}

rv_pccard_version_case rv_pccard_classify_version(uint32_t file_version, uint32_t console_version)
{
    const uint32_t file_major = file_version >> 16;
    const uint32_t file_minor = file_version & 0xFFFFu;
    const uint32_t console_major = console_version >> 16;

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
    static constexpr uint8_t magic[8] = { 'M', 'P', 'P', 'C', 'C', 'A', 'R', 'D' };
    std::memcpy(image.data(), magic, sizeof(magic));
    put_u32(image.data() + 8, console_version);
    put_u32(image.data() + 12, 0);
    put_i64(image.data() + 16, slot_count);
    put_i64(image.data() + 24, slot_size);
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
