// Version comparison and older-major migration for rv_pccard's image, split
// out of rv_pccard.cpp to keep that file under its line budget. Pure data
// transforms only - no filesystem or logging, so rv_pccard.cpp keeps owning
// I/O and log wording.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rv_3dmppc
{

// "M.m" from a packed major<<16|minor version, for log/refusal messages.
std::string rv_pccard_version_text(uint32_t v);

enum class rv_pccard_version_case {
    compatible,   // same major, older minor - this console's layout reads it
    migrate,      // older major - copy what fits into a fresh current card
    incompatible, // newer major, or same major newer minor - unreadable
};

// `file_version` must differ from `console_version` - callers handle the
// exact-match case themselves.
rv_pccard_version_case rv_pccard_classify_version(uint32_t file_version, uint32_t console_version);

// Migrates an older major's card, already read whole into `old_buffer`
// (`file_slots` x `file_slot_size`, `old_payload_offset` bytes of header plus
// length table ahead of the payload). This is the only container layout this
// code has ever known: when a future major changes it, whoever adds that
// major also adds reading of ITS predecessor's layout here.
//
// On success returns a freshly formatted current image (`slot_count` x
// `slot_size`, stamped `console_version`) with the old slots that fit copied
// in, `error` left empty, and one line per dropped slot/range appended to
// `warnings`. On a corrupt old image returns an empty vector and fills
// `error`.
std::vector<uint8_t> rv_pccard_migrate(const std::vector<uint8_t> &old_buffer,
    int64_t file_slots,
    int64_t file_slot_size,
    int64_t old_payload_offset,
    int64_t slot_count,
    int64_t slot_size,
    int64_t payload_offset,
    uint32_t console_version,
    std::string &error,
    std::vector<std::string> &warnings);

} // namespace rv_3dmppc
