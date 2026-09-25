// The embedded screen's development half (rv_pcframe.hpp): frames into the
// embedder's shared memory, pad buttons from the dev channel.
#include "rv_pconsole/platform/rv_pcframe.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

#include "pdk/rv_err.h"
#include "pdk/cio/rv_isource.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// The buttons the SDL window's keyboard gives, so a disc sees the same pad.
constexpr uint64_t RV_PCFRAME_KEYBOARD_ABILITIES = RV_ISOURCE_FRONT_BTTN_SOUTH | RV_ISOURCE_FRONT_BTTN_EAST
    | RV_ISOURCE_FRONT_BTTN_WEST | RV_ISOURCE_FRONT_BTTN_NORTH | RV_ISOURCE_BUMPER_LEFT
    | RV_ISOURCE_BUMPER_RIGHT | RV_ISOURCE_MENU_BTTN_MENU | RV_ISOURCE_MENU_BTTN_VIEW
    | RV_ISOURCE_DPAD_MOVE | RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_WEST
    | RV_ISOURCE_DPAD_EAST;

class rv_pcframe_window final : public rv_pcwindow
{
public:
    explicit rv_pcframe_window(int fd)
        : fd_(fd)
    {
    }

    ~rv_pcframe_window()
    {
        if (map_ != nullptr) {
            munmap(map_, size_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    int64_t open(const char *, int64_t w, int64_t h, uint64_t) override
    {
        if (w <= 0 || h <= 0) {
            return RV_ERR_INVAL;
        }

        uint64_t stride = w * 4;
        uint64_t slot_bytes = (RV_PCFRAME_SLOT_HEADER_BYTES + stride * h + 63) / 64 * 64;
        uint64_t size = RV_PCFRAME_HEADER_BYTES + RV_PCFRAME_SLOTS * slot_bytes;

        if (map_ != nullptr) {
            munmap(map_, size_);
            map_ = nullptr;
        }

        if (ftruncate(fd_, size) < 0) {
            RV_LOG_ERR("pcframe", "ftruncate failed: {}", strerror(errno));
            return RV_ERR_IO;
        }

        map_ = reinterpret_cast<uint8_t *>(
            mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (map_ == MAP_FAILED) {
            RV_LOG_ERR("pcframe", "mmap failed: {}", strerror(errno));
            map_ = nullptr;
            return RV_ERR_IO;
        }

        std::memset(map_, 0, size);

        auto *header = reinterpret_cast<rv_pcframe_header *>(map_);
        header->magic = RV_PCFRAME_MAGIC;
        header->version = RV_PCFRAME_VERSION;
        header->slot_count = RV_PCFRAME_SLOTS;
        header->slot_bytes = slot_bytes;
        header->latest = RV_PCFRAME_NONE;

        w_ = w;
        h_ = h;
        slot_bytes_ = slot_bytes;
        size_ = size;
        frames_ = 0;
        latest_ = RV_PCFRAME_NONE;

        return RV_OK;
    }

    bool presenting() const override
    {
        return map_ != nullptr;
    }

    void present(const uint32_t *argb) override
    {
        if (map_ == nullptr) {
            return;
        }

        uint32_t k = latest_ == RV_PCFRAME_NONE ? 0 : (latest_ + 1) % RV_PCFRAME_SLOTS;
        auto *slot = reinterpret_cast<rv_pcframe_slot *>(map_ + RV_PCFRAME_HEADER_BYTES + k * slot_bytes_);
        auto *header = reinterpret_cast<rv_pcframe_header *>(map_);

        std::atomic_ref<uint32_t> seq(slot->seq);
        const uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        slot->format = RV_PCFRAME_FORMAT_ARGB8888;
        slot->frame = ++frames_;
        slot->width = w_;
        slot->height = h_;
        slot->stride = w_ * 4;
        std::memcpy(reinterpret_cast<uint8_t *>(slot) + RV_PCFRAME_SLOT_HEADER_BYTES, argb,
            w_ * 4 * h_);

        seq.store(s + 2, std::memory_order_release);
        // A reader drops a copy whose seq was odd or changed, so the console never waits.
        std::atomic_ref<uint32_t>(header->latest).store(k, std::memory_order_release);
        latest_ = k;
    }

    bool close_requested() const override
    {
        return false;
    }

    uint64_t keyboard_abilities() const override
    {
        return RV_PCFRAME_KEYBOARD_ABILITIES;
    }

    rv_istate keyboard_state() const override
    {
        rv_istate s{};
        s.buttons = buttons_ & RV_PCFRAME_KEYBOARD_ABILITIES;
        return s;
    }

    rv_imouse consume_mouse() override
    {
        return rv_imouse{};
    }

    uint32_t consume_pause_requests() override
    {
        return 0;
    }

    void set_pad(uint64_t b)
    {
        buttons_ = b;
    }

    bool latest(uint32_t &slot, uint64_t &frame) const
    {
        if (frames_ == 0) {
            return false;
        }
        slot = latest_;
        frame = frames_;
        return true;
    }

private:
    int fd_;
    uint8_t *map_ = nullptr;
    uint64_t size_ = 0;
    uint64_t slot_bytes_ = 0;
    uint32_t w_ = 0;
    uint32_t h_ = 0;
    uint32_t latest_ = RV_PCFRAME_NONE;
    uint64_t frames_ = 0;
    uint64_t buttons_ = 0;
};

class rv_pcframe_platform final : public rv_pcplatform
{
public:
    rv_pcframe_platform(std::unique_ptr<rv_pcplatform> inner, int fd)
        : inner_(std::move(inner)), window_(fd)
    {
    }

    void pump() override
    {
        inner_->pump();
    }

    rv_pcwindow &window() override
    {
        return window_;
    }

    rv_pcgamepads &gamepads() override
    {
        return inner_->gamepads();
    }

    rv_pcaudio_sink &audio() override
    {
        return inner_->audio();
    }

    rv_pcframe_window &frame_window()
    {
        return window_;
    }

private:
    std::unique_ptr<rv_pcplatform> inner_;
    rv_pcframe_window window_;
};

} // namespace

std::unique_ptr<rv_pcplatform> rv_pcframe_wrap(std::unique_ptr<rv_pcplatform> inner, int fd)
{
    return std::make_unique<rv_pcframe_platform>(std::move(inner), fd);
}

bool rv_pcframe_set_pad(rv_pcplatform &platform, uint64_t buttons)
{
    auto *frame_platform = dynamic_cast<rv_pcframe_platform *>(&platform);
    if (frame_platform == nullptr) {
        return false;
    }
    frame_platform->frame_window().set_pad(buttons);
    return true;
}

bool rv_pcframe_latest(rv_pcplatform &platform, uint32_t &slot, uint64_t &frame)
{
    auto *frame_platform = dynamic_cast<rv_pcframe_platform *>(&platform);
    if (frame_platform == nullptr) {
        return false;
    }
    return frame_platform->frame_window().latest(slot, frame);
}

} // namespace rv_3dmppc
