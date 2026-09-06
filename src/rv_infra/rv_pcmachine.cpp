#include "rv_pcmachine.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

int64_t rv_pcmachine_available_ram()
{
    std::FILE *file = std::fopen("/proc/meminfo", "r");
    if (nullptr == file) {
        RV_LOG_WARN("pcmachine", "cannot open /proc/meminfo");
        return -1;
    }

    char line[256];
    long long kib = -1;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
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
    if (kib > std::numeric_limits<int64_t>::max() / 1024) {
        RV_LOG_WARN("pcmachine", "MemAvailable value {} kB is out of range", kib);
        return -1;
    }

    return static_cast<int64_t>(kib) * 1024;
}

} // namespace rv_3dmppc
