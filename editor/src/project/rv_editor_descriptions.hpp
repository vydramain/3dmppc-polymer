#pragma once

// The component records of the last successful build, kept in the project's state_dir so components show their
// descriptions without a rebuild. The file format is described beside its code in rv_editor_descriptions.cpp.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_disc_hash/rv_disc_hash.hpp"
#include "pdkkit/component/rv_component_record.hpp"

namespace rv_editor
{

// What the build was: when it was captured, the checksum of its disc.so, and the PDK version its records were written with.
struct rv_editor_descriptions_mark {
    int64_t time = 0; // seconds since the Unix epoch
    std::array<unsigned char, rv_pdklib::RV_DISC_HASH_BYTES> disc_hash{};
    uint32_t pdk_major = 0;
    uint32_t pdk_minor = 0;
};

// Only the states this module decides; the build hook adds the ones that compare the snapshot with the sources.
enum class rv_editor_descriptions_state {
    loaded,      // the snapshot is usable
    none,        // no snapshot: components show as text
    pdk_mismatch // the snapshot is from another PDK version: not used, as with none
};

struct rv_editor_descriptions {
    rv_editor_descriptions_state state = rv_editor_descriptions_state::none;
    std::string reason; // why the snapshot is not used; empty when loaded
    rv_editor_descriptions_mark mark;
    std::vector<rv_pdkkit::ecs_component_record> records;
};

// Copies the records and the mark of the ELF image `disc_so` into the snapshot under `state_dir`, replacing the previous
// one. All records must share one PDK version, and the mark takes it. No records is RV_ERR_NOENT, as a missing section
// is. RV_OK, or an rv_err code with the reason in `error`; a refused or failed capture keeps the previous snapshot.
int rv_editor_descriptions_capture(const std::filesystem::path &disc_so,
    const std::filesystem::path &state_dir,
    std::string &error);

// Reads the snapshot under `state_dir` into `out`. No snapshot is RV_OK with state none. A snapshot of another PDK
// version is RV_OK with state pdk_mismatch. A damaged file is RV_ERR_INVAL with the reason; `out` is then state none.
int rv_editor_descriptions_load(const std::filesystem::path &state_dir, rv_editor_descriptions &out, std::string &error);

} // namespace rv_editor
