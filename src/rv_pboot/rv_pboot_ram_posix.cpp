// The machine's available RAM, from /proc/meminfo.
#include "rv_pboot_machine.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

constexpr int64_t BYTES_PER_KIB = 1024; // bytes in kibibyte
constexpr size_t PROCFS_LINE_MAX = 256; // bytes, line buffer for /proc/meminfo

} // namespace

int64_t rv_pboot_mode_available_ram()
{
    std::FILE *file = std::fopen("/proc/meminfo", "r");
    if (nullptr == file) {
        RV_LOG_WARN("pcmachine", "cannot open /proc/meminfo");
        return -1;
    }

    char line[PROCFS_LINE_MAX];
    long long kib = -1;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        if (std::sscanf(line, "MemAvailable: %lld kB", &kib) == 1) {
            break;
        }
    }
    std::fclose(file);

    if (kib < 0) {
        RV_LOG_WARN("pcmachine", "no MemAvailable line in /proc/meminfo");
        return -1;
    }

    // Guard the kB -> bytes multiply against wrapping int64_t on a garbage
    // value rather than silently returning a wrong (wrapped) byte count.
    if (kib > std::numeric_limits<int64_t>::max() / BYTES_PER_KIB) {
        RV_LOG_WARN("pcmachine", "MemAvailable value {} kB is out of range", kib);
        return -1;
    }

    return static_cast<int64_t>(kib) * BYTES_PER_KIB;
}

} // namespace rv_3dmppc
