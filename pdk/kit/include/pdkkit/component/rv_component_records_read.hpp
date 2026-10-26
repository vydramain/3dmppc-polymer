#pragma once

// Reads the component records a disc.so was built with from its pdkkit_components section. Only the editor calls it, on
// the image after a build. The image has no alignment guarantee, so each record is copied out before it is checked.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_disc_hash/rv_disc_hash.hpp"
#include "pdkkit/component/rv_component_record.hpp"

namespace rv_pdkkit
{

// Refuses a record whose magic or format version is not this build's; the reason names the record's index.
inline int ecs_record_check(const ecs_component_record &record, std::size_t index, std::string &error)
{
    const std::string where = "component record " + std::to_string(index);
    if (std::memcmp(record.magic, ecs_record_magic, sizeof(record.magic)) != 0) {
        error = where + " has a bad magic: it is not a component record";
        return RV_ERR_INVAL;
    }
    if (record.format_version != ecs_record_format_version) {
        error = where + " has a format version this build does not read";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Copies the records of the pdkkit_components section of the ELF64 image `elf` (`size` bytes) into `out`. The pdk version
// of each record is kept as read: the caller decides whether it differs from this build's. On error `out` is untouched.
inline int
ecs_records_read(const unsigned char *elf, std::size_t size, std::vector<ecs_component_record> &out, std::string &error)
{
    bool found = false;
    std::uint64_t sh_offset = 0;
    std::uint64_t sh_size = 0;
    std::uint32_t sh_type = 0;
    const int find_rv =
        rv_pdklib::rv_disc_hash_find_section(elf, size, PDKKIT_RECORD_SECTION, found, sh_offset, sh_size, sh_type, error);
    if (find_rv != RV_OK) {
        return find_rv;
    }
    if (!found) {
        error = "the disc has no component records";
        return RV_ERR_NOENT;
    }
    if (sh_type == rv_pdklib::RV_ELF_SHT_NOBITS) {
        error = "the component records section has no bytes in the image";
        return RV_ERR_INVAL;
    }
    if (sh_size % sizeof(ecs_component_record) != 0) {
        error = "the component records section is not a whole number of records";
        return RV_ERR_INVAL;
    }

    // A zero-size section is an empty list, not an error: a disc with no components may have none, and the caller
    // should treat it like a missing section.
    const std::size_t count = sh_size / sizeof(ecs_component_record);
    std::vector<ecs_component_record> records(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(&records[i], elf + sh_offset + i * sizeof(ecs_component_record), sizeof(ecs_component_record));
        const int check_rv = ecs_record_check(records[i], i, error);
        if (check_rv != RV_OK) {
            return check_rv;
        }
    }

    out = std::move(records);
    return RV_OK;
}

} // namespace rv_pdkkit
