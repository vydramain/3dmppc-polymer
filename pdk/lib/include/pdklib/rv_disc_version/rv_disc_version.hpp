#pragma once

#include <cstdint>

#include "pdk/de/rv_dv.h"

namespace rv_pdklib
{

// The complete note record — what physically sits in the section: a header (a
// reader overlays Elf64_Nhdr onto it), the owner name, the payload. The header
// fields are uint32_t even in ELF64 (elf(5)). The name is an inline array and
// not a pointer: the characters themselves have to land in the file, not an
// address.
struct rv_mppc_note {
    uint32_t namesz, descsz, type;
    char name[sizeof(RV_MPPC_NOTE_OWNER_DEF)];
    rv_mppc_note_desc desc;
};

// The layout has to match what rv_pcloader::pre_dlopen_check reads: 12 bytes of
// header + the name (a multiple of 4, padded inside the array) + 16 bytes of
// payload, with no alignment holes between the fields.
static_assert(sizeof(RV_MPPC_NOTE_OWNER_DEF) % 4 == 0,
    "note owner length must be a multiple of 4; pad the string or fix the reader walk");
static_assert(sizeof(rv_mppc_note) ==
    12 + sizeof(RV_MPPC_NOTE_OWNER_DEF) + sizeof(rv_mppc_note_desc));

} // namespace rv_pdklib

// Plants the note record into the disc. Called once, from the translation unit
// the burner generates — a game's own sources know nothing about the version.
// The major/minor values come from the PDK headers the disc is built against, so
// what lands in the note is "the PDK version this disc was compiled against".
#define RV_MPPC_DISC_VERSION_DEF                                              \
    __attribute__((section(RV_MPPC_SECTION_NAME_DEF), used, retain)) alignas( \
        4) static const rv_pdklib::rv_mppc_note rv_mppc_disc_version_note = { \
        sizeof(RV_MPPC_NOTE_OWNER_DEF),                                       \
        sizeof(rv_mppc_note_desc),                                            \
        RV_MPPC_NOTE_TYPE,                                                    \
        RV_MPPC_NOTE_OWNER_DEF,                                               \
        { RV_MPPC_NOTE_MAGIC_DEF, static_cast<uint32_t>(RV_MPPC_VER_MAJOR),   \
            static_cast<uint32_t>(RV_MPPC_VER_MINOR) }                        \
    }
