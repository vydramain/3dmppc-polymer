#include "rv_pboot_mode.hpp"

#include <cstdio>
#include <limits>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pboot_args.hpp"
#include "rv_pboot_modes.hpp"
#include "rv_pconsole/rv_pchost_sdl3.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pmem/rv_pcvmem.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_mode_available_ram()
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

int64_t rv_pboot_mode_prepare(const rv_pboot_args &args, const rv_pcslots &slots, rv_pchost_sdl3 &host,
    rv_pboot_mode_info &out)
{
    // This console has no fallback and no override flag for a kernel that
    // lacks MADV_POPULATE_WRITE: it is a hard requirement of the vmem the
    // memory stages below reserve.
    if (rv_pcvmem_probe_populate_write() < 0) {
        rv_console_print_error(
            "this console requires a Linux kernel that implements MADV_POPULATE_WRITE");
        return RV_ERR_INVAL;
    }

    // An external disc will need its code extracted to a staging directory
    // before anything else about this run is built. Refuse now, before the
    // host, the loader or the console exist, rather than discovering the
    // directory is unusable deep inside bring_up().
    if (args.disc_path != nullptr && rv_pcloader_probe_staging() < 0) {
        rv_console_print_error("no usable staging directory for the disc's code");
        return RV_ERR_INVAL;
    }

    host.prepare(slots.cv == rv_pccv_impl::sdl3, slots.cio == rv_pccio_impl::sdl3,
        slots.ca == rv_pcca_impl::sdl3);

    // What this run's machine actually is: which subsystems came up (or were
    // never asked for) and how much RAM the kernel says is available.
    out.ram_available = rv_pboot_mode_available_ram();
    out.audio_enabled = slots.ca == rv_pcca_impl::sdl3 && host.audio_ready();

    return RV_OK;
}

void rv_pboot_mode_report(const rv_pboot_args &args, const rv_pcslots &slots, const rv_pchost_sdl3 &host,
    const rv_pboot_mode_info &machine)
{
    RV_LOG_INFO("main", "mode '{}' prepared: ca={} cv={} cio={} cl={}", args.mode,
        rv_pboot_impl_name(slots.ca), rv_pboot_impl_name(slots.cv), rv_pboot_impl_name(slots.cio),
        rv_pboot_impl_name(slots.cl));
    if (slots.cv != rv_pccv_impl::sdl3) {
        RV_LOG_INFO("main", "video: off (cv is null)");
    } else if (!host.video_ready()) {
        RV_LOG_INFO("main", "video: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "video: on");
    }
    if (slots.cio != rv_pccio_impl::sdl3) {
        RV_LOG_INFO("main", "gamepad: off (cio is null)");
    } else if (!host.gamepad_ready()) {
        RV_LOG_INFO("main", "gamepad: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "gamepad: on");
    }
    if (slots.ca != rv_pcca_impl::sdl3) {
        RV_LOG_INFO("main", "audio: off (ca is null)");
    } else if (!host.audio_ready()) {
        RV_LOG_INFO("main", "audio: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "audio: on");
    }
    RV_LOG_INFO("main", "ram available: {}", machine.ram_available);
}

} // namespace rv_3dmppc
