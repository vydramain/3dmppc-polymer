#include "project/rv_editor_descriptions.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_records_read.hpp"
#include "project/rv_editor_toml.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// The snapshot is state_dir/descriptions/components.snap: a snapshot_header, then `count` records copied as they sit in
// disc.so. This is machine-local per-user state under XDG_STATE_HOME; the file is not portable. magic and file_format
// identify the file; pdk_major/pdk_minor are checked before any record is read, since another PDK's record layout is
// not this build's.
constexpr std::string_view dir_name = "descriptions";
constexpr std::string_view file_name = "components.snap";
constexpr char snapshot_magic[] = "PDKKDSC"; // seven letters and the NUL: fills snapshot_header::magic
constexpr uint32_t snapshot_file_format = 1;

struct snapshot_header {
    char magic[8];
    uint32_t file_format;
    uint32_t pad = 0; // explicit: keeps time at offset 16 with no hidden padding bytes
    int64_t time;
    unsigned char disc_hash[rv_pdklib::RV_DISC_HASH_BYTES];
    uint32_t pdk_major;
    uint32_t pdk_minor;
    uint64_t count;
};
static_assert(sizeof(snapshot_header) == 48, "snapshot_header is the file's layout");
static_assert(offsetof(snapshot_header, time) == 16, "snapshot_header is the file's layout");
static_assert(sizeof(snapshot_magic) == sizeof(snapshot_header::magic), "snapshot_magic must fill magic exactly");

std::filesystem::path snapshot_path(const std::filesystem::path &state_dir)
{
    return state_dir / std::string(dir_name) / std::string(file_name);
}

std::string pdk_text(uint32_t major, uint32_t minor)
{
    return std::to_string(major) + "." + std::to_string(minor);
}

// The records of one build share a PDK version, which the mark takes. No records is RV_ERR_NOENT, as a missing
// section is: a disc with no components has nothing to describe.
int records_pdk_version(const std::vector<rv_pdkkit::ecs_component_record> &records,
    uint32_t &major,
    uint32_t &minor,
    std::string &error)
{
    if (records.empty()) {
        error = "the disc has no component records";
        return RV_ERR_NOENT;
    }
    major = records.front().pdk_major;
    minor = records.front().pdk_minor;
    for (std::size_t i = 1; i < records.size(); ++i) {
        if (records[i].pdk_major != major || records[i].pdk_minor != minor) {
            error = "component record " + std::to_string(i) + " is PDK " +
                pdk_text(records[i].pdk_major, records[i].pdk_minor) + ", record 0 is PDK " + pdk_text(major, minor);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Fills `out` from the snapshot's bytes; the caller resets it on error.
int snapshot_parse(const std::string &bytes, rv_editor_descriptions &out, std::string &error)
{
    if (bytes.size() < sizeof(snapshot_header)) {
        error = "damaged or unreadable: shorter than the snapshot header";
        return RV_ERR_INVAL;
    }
    snapshot_header header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::memcmp(header.magic, snapshot_magic, sizeof(header.magic)) != 0 || header.file_format != snapshot_file_format) {
        error = "damaged: not a descriptions snapshot";
        return RV_ERR_INVAL;
    }

    out.mark.time = header.time;
    std::memcpy(out.mark.disc_hash.data(), header.disc_hash, sizeof(header.disc_hash));
    out.mark.pdk_major = header.pdk_major;
    out.mark.pdk_minor = header.pdk_minor;
    if (header.pdk_major != RV_MPPC_VER_MAJOR || header.pdk_minor != RV_MPPC_VER_MINOR) {
        const std::string written = pdk_text(header.pdk_major, header.pdk_minor);
        const std::string built = pdk_text(RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR);
        out.state = rv_editor_descriptions_state::pdk_mismatch;
        out.reason = rv_editor_text_format("descriptions.pdk_mismatch", std::make_format_args(written, built));
        return RV_OK;
    }

    const std::size_t body = bytes.size() - sizeof(snapshot_header);
    const std::size_t record_size = sizeof(rv_pdkkit::ecs_component_record);
    if (body % record_size != 0 || header.count != body / record_size) {
        error = "damaged: the record count does not match the file size";
        return RV_ERR_INVAL;
    }

    const std::size_t count = body / record_size;
    out.records.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(&out.records[i], bytes.data() + sizeof(snapshot_header) + i * record_size, record_size);
        const int check_rv = rv_pdkkit::ecs_record_check(out.records[i], i, error);
        if (check_rv != RV_OK) {
            return check_rv;
        }
    }

    out.state = rv_editor_descriptions_state::loaded;
    out.reason.clear();
    return RV_OK;
}

} // namespace

int rv_editor_descriptions_capture(const std::filesystem::path &disc_so,
    const std::filesystem::path &state_dir,
    std::string &error)
{
    const std::string image = rv_editor_file_text(disc_so);
    if (image.empty()) {
        error = "cannot read " + disc_so.string();
        return RV_ERR_IO;
    }
    const auto *elf = reinterpret_cast<const unsigned char *>(image.data());

    std::vector<rv_pdkkit::ecs_component_record> records;
    const int records_rv = rv_pdkkit::ecs_records_read(elf, image.size(), records, error);
    if (records_rv != RV_OK) {
        return records_rv;
    }
    uint32_t pdk_major = 0;
    uint32_t pdk_minor = 0;
    const int version_rv = records_pdk_version(records, pdk_major, pdk_minor, error);
    if (version_rv != RV_OK) {
        return version_rv;
    }

    snapshot_header header{};
    const int hash_rv = rv_pdklib::rv_disc_hash_compute(elf, image.size(), header.disc_hash, error);
    if (hash_rv != RV_OK) {
        return hash_rv;
    }

    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    std::memcpy(header.magic, snapshot_magic, sizeof(header.magic));
    header.file_format = snapshot_file_format;
    header.time = std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count();
    header.pdk_major = pdk_major;
    header.pdk_minor = pdk_minor;
    header.count = records.size();

    std::string text(reinterpret_cast<const char *>(&header), sizeof(header));
    text.append(reinterpret_cast<const char *>(records.data()), records.size() * sizeof(rv_pdkkit::ecs_component_record));
    const int write_rv = rv_editor_file_replace(snapshot_path(state_dir), text, error);
    if (write_rv != RV_OK) {
        return write_rv;
    }
    return RV_OK;
}

int rv_editor_descriptions_load(const std::filesystem::path &state_dir, rv_editor_descriptions &out, std::string &error)
{
    out = rv_editor_descriptions{};
    const std::filesystem::path path = snapshot_path(state_dir);
    std::error_code ec;
    const bool present = std::filesystem::exists(path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (!present) {
        out.reason = rv_editor_text("descriptions.no_snapshot");
        return RV_OK;
    }

    const int parse_rv = snapshot_parse(rv_editor_file_text(path), out, error);
    if (parse_rv != RV_OK) {
        out = rv_editor_descriptions{};
        error = path.string() + ": " + error;
        return parse_rv;
    }
    return RV_OK;
}

} // namespace rv_editor
