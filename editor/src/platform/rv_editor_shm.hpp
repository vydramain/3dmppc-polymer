#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rv_editor
{

// The Game frame's shared memory (docs/adr/0006-game-frame.md): made here, handed
// to the console as --frame-fd, read without ever making the console wait. The
// layout is the console's (src/rv_pconsole/platform/rv_pcframe.hpp, version 1).
class rv_editor_frame_memory
{
public:
    rv_editor_frame_memory() = default;
    rv_editor_frame_memory(const rv_editor_frame_memory &) = delete;
    rv_editor_frame_memory &operator=(const rv_editor_frame_memory &) = delete;
    ~rv_editor_frame_memory();

    // A fresh, empty memory object, replacing any earlier one. Its descriptor is
    // close-on-exec and never below 10, so a child gets it only as asked. False
    // with the reason.
    bool create(std::string &error);
    void close();
    int fd() const { return fd_; }

    // Copies the newest finished frame when its count is above `frame`: true then,
    // with `frame`, `width`, `height` and `pixels` (0xAARRGGBB, width * height)
    // set. False when there is none yet, the memory is not a version this reads,
    // or the copy overlapped the console's write of that slot.
    bool read(uint64_t &frame, uint32_t &width, uint32_t &height, std::vector<uint32_t> &pixels);

private:
    int fd_ = -1;
    void *map_ = nullptr;
    size_t size_ = 0;
};

} // namespace rv_editor
