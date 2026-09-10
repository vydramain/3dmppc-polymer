// A single mmap reservation whose pages become usable a piece at a time.
// Reserving up front (PROT_NONE, no physical pages) and ensuring on demand
// (mprotect + MADV_POPULATE_WRITE) lets a caller size a vmem for its worst
// case without paying for it until it is actually touched.
#pragma once

#include <cstdint>

namespace rv_3dmppc
{

// Reserved address space, made usable a piece at a time.
class rv_pcvmem
{
public:
    // Reserves `reserve_bytes` of address space, PROT_NONE. Nothing is usable
    // yet. A zero or negative size reserves nothing and stays valid().
    explicit rv_pcvmem(int64_t reserve_bytes);
    ~rv_pcvmem();

    rv_pcvmem(const rv_pcvmem &) = delete;
    rv_pcvmem &operator=(const rv_pcvmem &) = delete;

    // False when the reservation itself failed.
    bool valid() const;

    int64_t reserved() const;
    int64_t ensured() const;

    // Base of the reservation. Only the first ensured() bytes may be touched.
    uint8_t *base() const;

    // Make sure the first `bytes` of the reservation are readable and writable
    // and their pages are populated. Monotonic: a request below ensured() is
    // a no-op returning RV_OK. Returns RV_OK, RV_ERR_INVAL when `bytes` is
    // negative or above reserved(), or RV_ERR_NOMEM when the pages cannot be
    // prepared. On failure ensured() is unchanged and NOTHING new is handed
    // out, even if madvise populated part of the range.
    int64_t ensure(int64_t bytes);

private:
    uint8_t *base_ = nullptr;
    int64_t reserved_ = 0;
    int64_t ensured_ = 0;
    bool valid_ = true;
};

// Does this kernel actually implement MADV_POPULATE_WRITE? The constant being
// present in the headers is not enough. Performs a real one-page probe.
// Returns RV_OK, or RV_ERR_NOENT when the kernel rejects it.
int64_t rv_pcvmem_probe_populate_write();

// Self-check for rv_pcvmem. Uses assert; aborts on first violation. Nothing
// in the tree calls this yet.
bool rv_pcvmem_selfcheck();

} // namespace rv_3dmppc
