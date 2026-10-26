#pragma once

// The record PDKKIT_COMPONENT leaves in disc.so's pdkkit_components section: a header, then the component's desc. The
// editor reads it by magic and format version, so a layout change bumps ecs_record_format_version. The section is outside
// the disc checksum (rv_disc_hash covers .text, .rodata, .data) on purpose: the console never reads or executes it, and
// only the editor reads it after its own build.

#include <cstddef>
#include <cstdint>

#include "pdk/de/rv_dv.h"
#include "pdkkit/component/rv_component_desc.hpp"

namespace rv_pdkkit
{

// Version of ecs_component_record's layout (the header and the desc it carries).
inline constexpr uint32_t ecs_record_format_version = 1;

// The attribute takes a string literal, so the section name is a macro: "pdkkit_components" in disc.so.
#define PDKKIT_RECORD_SECTION "pdkkit_components"

// Magic of every record, NUL included: a reader finds records by it.
inline constexpr char ecs_record_magic[] = "PDKKCMP";

struct ecs_component_record {
    char magic[8];
    uint32_t format_version;
    uint32_t pdk_major;
    uint32_t pdk_minor;
    // explicit: the 4 bytes desc's alignment would add anyway, named and zeroed so the record has no indeterminate header bytes
    uint32_t pad;
    ecs_component_desc desc;
};

static_assert(sizeof(ecs_record_magic) == sizeof(ecs_component_record::magic), "ecs_record_magic must fill magic exactly");
static_assert(sizeof(ecs_component_record) == 1184, "ecs_component_record is the section's layout");
static_assert(offsetof(ecs_component_record, desc) == 24, "ecs_component_record is the section's layout");

// The record of desc. memcpy is not constexpr, so magic is copied by loop.
constexpr ecs_component_record ecs_record_make(const ecs_component_desc &desc)
{
    ecs_component_record record{
        .magic = {},
        .format_version = ecs_record_format_version,
        .pdk_major = RV_MPPC_VER_MAJOR,
        .pdk_minor = RV_MPPC_VER_MINOR,
        .pad = 0,
        .desc = desc,
    };
    for (std::size_t i = 0; i < sizeof(record.magic); ++i) {
        record.magic[i] = ecs_record_magic[i];
    }
    return record;
}

} // namespace rv_pdkkit
