#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rv_pdklib
{

// Size of the checksum written into rv_mppc_note_desc::magic.
inline constexpr std::size_t RV_DISC_HASH_BYTES = 8;

// ELF specification value of SHT_NOBITS (a section with no bytes in the image); elf.h stays in the .cpp.
inline constexpr std::uint32_t RV_ELF_SHT_NOBITS = 8;

// Compute the disc code checksum over the ELF64 image in `elf` (`elf_size`
// bytes) and write RV_DISC_HASH_BYTES bytes into `out`. Returns RV_OK on
// success; on failure returns RV_ERR_INVAL and fills `error` with one sentence.
// Never reads outside [elf, elf + elf_size).
int rv_disc_hash_compute(const unsigned char *elf,
    std::size_t elf_size,
    unsigned char out[RV_DISC_HASH_BYTES],
    std::string &error);

// File offset of rv_mppc_note_desc::magic inside an ELF64 image, i.e. where the
// burner writes the checksum and where a reader would find it. Locates the
// section named RV_MPPC_SECTION_NAME_DEF (".note.rv_mppc_ver"), parses the
// Elf64_Nhdr inside it, and checks the note's owner and descriptor size.
// Returns RV_OK and sets `magic_offset`, or RV_ERR_INVAL with a one-sentence `error`.
int rv_disc_hash_magic_offset(const unsigned char *elf, std::size_t elf_size, std::size_t &magic_offset, std::string &error);

// Finds the section named `name` in an ELF64 image. Returns RV_OK; a missing section gives found=false.
// sh_offset, sh_size, sh_type are set only on RV_OK with found=true; otherwise they are untouched.
// Returns RV_ERR_INVAL and a one-sentence `error` for a malformed image or out-of-bounds data.
// SHT_NOBITS sections are not checked against elf_size and have no bytes in the image: never read through them.
// Never reads outside [elf, elf + elf_size).
int rv_disc_hash_find_section(const unsigned char *elf,
    std::size_t elf_size,
    const char *name,
    bool &found,
    std::uint64_t &sh_offset,
    std::uint64_t &sh_size,
    std::uint32_t &sh_type,
    std::string &error);

} // namespace rv_pdklib
