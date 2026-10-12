// The machine's available RAM in a browser: there is no /proc/meminfo there,
// so the wasm heap limit stands in for it.
#include "rv_pboot_machine.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#include <emscripten/heap.h>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_mode_available_ram()
{
    const size_t heap_max = emscripten_get_heap_max();
    if (heap_max > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
        RV_LOG_WARN("pcmachine", "wasm heap limit {} bytes is out of range", heap_max);
        return -1;
    }

    return static_cast<int64_t>(heap_max);
}

} // namespace rv_3dmppc
