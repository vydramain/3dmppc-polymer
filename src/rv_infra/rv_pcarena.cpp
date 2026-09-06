#include "rv_pcarena.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "pdk/rv_err.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif

namespace rv_3dmppc {

namespace {

int64_t page_size() {
    static const int64_t size = sysconf(_SC_PAGESIZE);
    return size > 0 ? size : 4096;
}

int64_t round_up_to_page(int64_t bytes) {
    const int64_t page = page_size();
    const int64_t remainder = bytes % page;
    return remainder == 0 ? bytes : bytes + (page - remainder);
}

}  // namespace

rv_pcarena::rv_pcarena(int64_t reserve_bytes) {
    if (reserve_bytes <= 0) {
        return;
    }

    const int64_t rounded = round_up_to_page(reserve_bytes);
    void *mapping = mmap(nullptr, static_cast<size_t>(rounded), PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        RV_LOG_ERR("pcarena", "mmap of {} bytes failed: {}", rounded, strerror(errno));
        valid_ = false;
        return;
    }

    base_ = static_cast<uint8_t *>(mapping);
    reserved_ = rounded;
}

rv_pcarena::~rv_pcarena() {
    if (base_ != nullptr) {
        munmap(base_, static_cast<size_t>(reserved_));
    }
}

bool rv_pcarena::valid() const { return valid_; }

int64_t rv_pcarena::reserved() const { return reserved_; }

int64_t rv_pcarena::committed() const { return committed_; }

uint8_t *rv_pcarena::base() const { return base_; }

int64_t rv_pcarena::commit(int64_t bytes) {
    if (bytes < 0 || bytes > reserved_) {
        return rv_pdk::RV_ERR_INVAL;
    }

    const int64_t target = round_up_to_page(bytes);
    if (target <= committed_) {
        return rv_pdk::RV_OK;
    }

    // Rounding must never hand out more than was reserved.
    const int64_t clamped_target = target > reserved_ ? reserved_ : target;
    const int64_t delta = clamped_target - committed_;
    uint8_t *region = base_ + committed_;

    if (mprotect(region, static_cast<size_t>(delta), PROT_READ | PROT_WRITE) != 0) {
        RV_LOG_ERR("pcarena", "mprotect of {} bytes failed: {}", delta, strerror(errno));
        return rv_pdk::RV_ERR_NOMEM;
    }

    if (madvise(region, static_cast<size_t>(delta), MADV_POPULATE_WRITE) != 0) {
        RV_LOG_ERR("pcarena", "madvise(POPULATE_WRITE) of {} bytes failed: {}", delta,
            strerror(errno));
        // Undo the mprotect so nothing new is handed out on failure.
        mprotect(region, static_cast<size_t>(delta), PROT_NONE);
        return rv_pdk::RV_ERR_NOMEM;
    }

    committed_ = clamped_target;
    return rv_pdk::RV_OK;
}

int64_t rv_pcarena_probe_populate_write() {
    const int64_t page = page_size();
    void *mapping = mmap(
        nullptr, static_cast<size_t>(page), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        RV_LOG_ERR("pcarena", "probe mmap failed: {}", strerror(errno));
        return rv_pdk::RV_ERR_NOMEM;
    }

    const int64_t result =
        madvise(mapping, static_cast<size_t>(page), MADV_POPULATE_WRITE) == 0
            ? rv_pdk::RV_OK
            : rv_pdk::RV_ERR_NOENT;
    munmap(mapping, static_cast<size_t>(page));
    return result;
}

}  // namespace rv_3dmppc
