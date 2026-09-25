// The Game frame's shared memory on Linux: memfd_create, read-only mapping, seqlock copy.

#include "platform/rv_editor_shm.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rv_editor
{

namespace
{

// The console's layout, src/rv_pconsole/platform/rv_pcframe.hpp: header words
// magic, version, slot_count, slot_bytes, latest; slot fields seq, format, frame
// (64-bit), width, height, stride, then pixels.
constexpr uint32_t RV_PCFRAME_MAGIC = 0x42465652;
constexpr uint32_t RV_PCFRAME_VERSION = 1;
constexpr uint32_t RV_PCFRAME_SLOTS = 3;
constexpr uint32_t RV_PCFRAME_HEADER_BYTES = 64;
constexpr uint32_t RV_PCFRAME_SLOT_HEADER_BYTES = 32;
constexpr uint32_t RV_PCFRAME_FORMAT_ARGB8888 = 1;

} // namespace

rv_editor_frame_memory::~rv_editor_frame_memory()
{
    close();
}

bool rv_editor_frame_memory::create(std::string &error)
{
    close();
    int fd = memfd_create("rv-editor-frame", MFD_CLOEXEC);
    if (fd < 0) {
        error = std::string("memfd_create: ") + std::strerror(errno);
        return false;
    }
    // posix_spawn's dup2 onto descriptor 3 must never be dup2(3, 3), which would keep close-on-exec.
    if (fd < 10) {
        const int high = fcntl(fd, F_DUPFD_CLOEXEC, 10);
        ::close(fd);
        if (high < 0) {
            error = std::string("fcntl: ") + std::strerror(errno);
            return false;
        }
        fd = high;
    }
    fd_ = fd;
    return true;
}

void rv_editor_frame_memory::close()
{
    if (map_ != nullptr) {
        munmap(map_, size_);
        map_ = nullptr;
        size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool rv_editor_frame_memory::read(uint64_t &frame, uint32_t &width, uint32_t &height, std::vector<uint32_t> &pixels)
{
    if (fd_ < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd_, &st) < 0) {
        return false;
    }
    const size_t file_size = static_cast<size_t>(st.st_size);
    if (file_size < RV_PCFRAME_HEADER_BYTES) {
        return false;
    }

    // The console sizes the object when it opens its screen, and again if the screen changes.
    if (file_size != size_) {
        if (map_ != nullptr) {
            munmap(map_, size_);
        }
        map_ = mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd_, 0);
        if (map_ == MAP_FAILED) {
            map_ = nullptr;
            size_ = 0;
            return false;
        }
        size_ = file_size;
    }

    const uint8_t *base = static_cast<const uint8_t *>(map_);
    auto load32 = [base](uint64_t offset, std::memory_order order) {
        return std::atomic_ref<uint32_t>(*const_cast<uint32_t *>(reinterpret_cast<const uint32_t *>(base + offset)))
            .load(order);
    };

    uint32_t magic = load32(0, std::memory_order_acquire);
    if (magic != RV_PCFRAME_MAGIC) {
        return false;
    }

    uint32_t version = load32(4, std::memory_order_relaxed);
    if (version != RV_PCFRAME_VERSION) {
        return false;
    }

    uint32_t slot_count = load32(8, std::memory_order_relaxed);
    if (slot_count != RV_PCFRAME_SLOTS) {
        return false;
    }

    uint32_t slot_bytes = load32(12, std::memory_order_relaxed);
    if (size_ < RV_PCFRAME_HEADER_BYTES + uint64_t(RV_PCFRAME_SLOTS) * slot_bytes) {
        return false;
    }

    uint32_t latest = load32(16, std::memory_order_acquire);
    if (latest >= RV_PCFRAME_SLOTS) {
        return false;
    }

    uint64_t slot_offset = RV_PCFRAME_HEADER_BYTES + uint64_t(latest) * slot_bytes;

    // A slot the console is writing has an odd seq.
    uint32_t seq1 = load32(slot_offset + 0, std::memory_order_acquire);
    if (seq1 & 1) {
        return false;
    }

    uint32_t format;
    uint64_t frame_num;
    uint32_t w, h, stride;

    std::memcpy(&format, base + slot_offset + 4, sizeof(format));
    std::memcpy(&frame_num, base + slot_offset + 8, sizeof(frame_num));
    std::memcpy(&w, base + slot_offset + 16, sizeof(w));
    std::memcpy(&h, base + slot_offset + 20, sizeof(h));
    std::memcpy(&stride, base + slot_offset + 24, sizeof(stride));

    if (frame_num <= frame) {
        return false;
    }
    if (format != RV_PCFRAME_FORMAT_ARGB8888) {
        return false;
    }
    if (w == 0 || h == 0) {
        return false;
    }
    if (stride != w * 4) {
        return false;
    }
    if (RV_PCFRAME_SLOT_HEADER_BYTES + uint64_t(stride) * h > slot_bytes) {
        return false;
    }

    std::vector<uint32_t> copy(size_t(w) * h);
    std::memcpy(copy.data(), base + slot_offset + RV_PCFRAME_SLOT_HEADER_BYTES, size_t(stride) * h);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint32_t seq2 = load32(slot_offset + 0, std::memory_order_relaxed);
    if (seq1 != seq2) {
        // The console wrote this slot while it was copied; the next frame will do.
        return false;
    }

    frame = frame_num;
    width = w;
    height = h;
    pixels.swap(copy);
    return true;
}

} // namespace rv_editor
