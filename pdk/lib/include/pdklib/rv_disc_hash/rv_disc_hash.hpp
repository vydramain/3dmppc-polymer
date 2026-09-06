#pragma once

#include <cstddef>
#include <string>

namespace rv_pdklib
{

// Size of the checksum written into rv_mppc_note_desc::magic.
inline constexpr std::size_t RV_DISC_HASH_BYTES = 8;

// Compute the disc code checksum over the ELF64 image in `elf` (`elf_size`
// bytes) and write RV_DISC_HASH_BYTES bytes into `out`. Returns true on
// success; on failure returns false and fills `error` with one sentence.
// Never reads outside [elf, elf + elf_size).
bool rv_disc_hash_compute(const unsigned char *elf, std::size_t elf_size,
    unsigned char out[RV_DISC_HASH_BYTES], std::string &error);

// File offset of rv_mppc_note_desc::magic inside an ELF64 image, i.e. where the
// burner writes the checksum and where a reader would find it. Locates the
// section named RV_MPPC_SECTION_NAME_DEF (".note.rv_mppc_ver"), parses the
// Elf64_Nhdr inside it, and checks the note's owner and descriptor size.
// Returns true and sets `magic_offset`, or false with a one-sentence `error`.
bool rv_disc_hash_magic_offset(const unsigned char *elf, std::size_t elf_size,
    std::size_t &magic_offset, std::string &error);

} // namespace rv_pdklib
