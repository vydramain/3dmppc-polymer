// A private memory pool the console hands out OPAQUE ADDRESSES into. Both of
// the console's memories are this shape, word for word from the two
// contracts: video RAM (rv_cv::video_asset_malloc / _write / _free) and sound
// RAM (rv_ca::sound_asset_malloc / _write / _free) — reserve a region, fill it,
// release it; the address is an offset, never a pointer; exhaustion is
// RV_ERR_NOMEM, not a host allocation.
//
// The backing store is an rv_pcarena: construction only RESERVES address
// space, and physical pages are committed only for the bytes a malloc() has
// actually handed out. The arena may round its reservation up to a page
// boundary, but that rounding is a host implementation detail and never
// enlarges capacity() — every block bound is measured against the size the
// caller asked for.
//
// Extracted from the video pool once the audio stage needed the same thing. The
// alternative — two near-identical allocators — would mean fixing every
// fragmentation bug twice.
//
// PATTERN: free-list allocator (object pool). One flat, offset-ordered vector of
// blocks covers the whole pool with no gaps; allocation splits a free block and
// release merges neighbours back. A general-purpose allocator would work too,
// but the console must be able to answer "is this address a live region?" for
// every primitive or voice that names one, and that question IS the block list.
//
// PATTERN: policy through a metadata type. What a region MEANS differs per
// memory — a texture's format and shape for video, a sample's length for audio —
// so the pool carries a caller-chosen `Meta` next to each block instead of
// knowing about either. The allocator stays ignorant of what it stores.
//
// A pool whose backing reservation FAILED is valid() == false. It still
// reports the capacity() it was asked for — that is what the disc declared,
// not what the host actually gave it — but hands out nothing: malloc() is
// RV_ERR_NOMEM immediately, before the block list is even consulted.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "pdk/rv_err.hpp"
#include "rv_pmem/rv_pcarena.hpp"

namespace rv_3dmppc
{

template <typename Meta>
class rv_pcpool
{
public:
    // `alignment` is the boundary every region starts on; `reserved_head` is a
    // prefix of the pool that is never handed out.
    rv_pcpool(int64_t size, int64_t alignment, int64_t reserved_head)
        : arena_(size > 0 ? size : 0)
        , capacity_(size > 0 ? size : 0)
        , alignment_(alignment > 0 ? alignment : 1)
    {
        // The block list only ever spans bytes the arena actually reserved. A
        // failed reservation reports its declared capacity() honestly but has
        // no usable free space to hand out: the whole pool is the (used,
        // reserved) head block below, sized 0, and nothing else.
        const int64_t total = arena_.valid() ? capacity() : 0;

        // The head block is `used` forever: allocation skips it, free() refuses
        // it, and coalescing stops at it. Keeping address 0 out of circulation
        // is what lets a zero-initialized rv_polygon::addr_texture (or an unset
        // rv_voice_conf::sample_address) read as "not set" instead of
        // accidentally naming a real region — the common case for a POD struct
        // that crosses the contract by value.
        rv_pcpool_block head;
        head.offset = 0;
        head.size = total < reserved_head ? total : reserved_head;
        head.used = true;
        head.reserved = true;
        blocks_.push_back(head);

        if (total > head.size) {
            rv_pcpool_block body;
            body.offset = head.size;
            body.size = total - head.size;
            body.used = false;
            blocks_.push_back(body);
        }
    }

    // Does this pool actually hold the space it was asked for? False when the
    // backing arena's reservation failed — capacity() still reports what was
    // asked for, but nothing is usable.
    bool valid() const
    {
        return arena_.valid();
    }

    // Reserve `size` bytes. Returns the region address (> 0), RV_ERR_INVAL when
    // `size` is not positive, or RV_ERR_NOMEM when no free block fits (which
    // includes a pool whose reservation failed: !valid() is checked first,
    // before the block list is even consulted).
    int64_t malloc(int64_t size)
    {
        if (!valid()) {
            return rv_pdk::RV_ERR_NOMEM;
        }
        if (size <= 0) {
            return rv_pdk::RV_ERR_INVAL;
        }

        const int64_t want = align_up(size);

        // First fit: walk the offset-ordered list and take the first free block
        // that is large enough. Best fit would waste less per call but leaves
        // the pool full of unusable slivers under this console's usage pattern
        // (a disc uploads its atlas once at load and rarely churns), and first
        // fit keeps the block list short — which every region_exists() pays for.
        for (size_t i = 0; i < blocks_.size(); ++i) {
            rv_pcpool_block &block = blocks_[i];
            if (block.used || block.size < want) {
                continue;
            }

            const int64_t addr = block.offset;
            const int64_t orig_size = block.size;
            const int64_t leftover = block.size - want;

            block.size = want;
            block.used = true;
            block.meta = Meta{};

            bool inserted_tail = false;
            if (leftover > 0) {
                rv_pcpool_block tail;
                tail.offset = addr + want;
                tail.size = leftover;
                tail.used = false;
                blocks_.insert(blocks_.begin() + static_cast<int64_t>(i) + 1, tail);
                inserted_tail = true;
            }

            // The block itself is only paperwork; the bytes it names must
            // actually be usable before a caller can touch them.
            if (arena_.commit(addr + want) != rv_pdk::RV_OK) {
                if (inserted_tail) {
                    blocks_.erase(blocks_.begin() + static_cast<int64_t>(i) + 1);
                }
                blocks_[i].size = orig_size;
                blocks_[i].used = false;
                blocks_[i].meta = Meta{};
                return rv_pdk::RV_ERR_NOMEM;
            }

            return addr;
        }

        return rv_pdk::RV_ERR_NOMEM;
    }

    // Release the region at `addr`. Returns RV_OK, or RV_ERR_INVAL for an
    // unknown address, a double free, or the reserved head.
    int64_t free(int64_t addr)
    {
        const int64_t at = find_block(addr);
        if (at < 0) {
            return rv_pdk::RV_ERR_INVAL;
        }

        const size_t index = static_cast<size_t>(at);
        if (!blocks_[index].used || blocks_[index].reserved) {
            return rv_pdk::RV_ERR_INVAL;
        }

        blocks_[index].used = false;
        blocks_[index].meta = Meta{};

        // THEOREM: coalescing preserves the invariant "no two adjacent free
        // blocks", and that invariant is what makes the largest free block as
        // large as the contiguous free space actually allows. Without merging,
        // the pool degrades into a chain of free slivers that no allocation fits
        // into even though the total free byte count is plenty — the classic
        // external-fragmentation death of a long-running allocator.
        if (index + 1 < blocks_.size() && !blocks_[index + 1].used) {
            blocks_[index].size += blocks_[index + 1].size;
            blocks_.erase(blocks_.begin() + static_cast<int64_t>(index) + 1);
        }
        if (index > 0 && !blocks_[index - 1].used) {
            blocks_[index - 1].size += blocks_[index].size;
            blocks_.erase(blocks_.begin() + static_cast<int64_t>(index));
        }

        return rv_pdk::RV_OK;
    }

    // Copy `bytes` of `data` into the region at `addr`. Returns RV_OK, or
    // RV_ERR_INVAL for an unknown address, a null source, or data that does not
    // fit the region. Both contracts promise the bytes are copied during the
    // call, so the disc may release its own buffer the moment this returns.
    int64_t write(int64_t addr, const void *data, int64_t bytes)
    {
        rv_pcpool_block *block = live_block(addr);
        if (!block) {
            return rv_pdk::RV_ERR_INVAL;
        }

        if (bytes < 0 || bytes > block->size) {
            return rv_pdk::RV_ERR_INVAL;
        }
        if (bytes > 0 && data == nullptr) {
            return rv_pdk::RV_ERR_INVAL;
        }

        if (bytes > 0) {
            std::memcpy(arena_.base() + block->offset, data, static_cast<size_t>(bytes));
        }
        return rv_pdk::RV_OK;
    }

    // Is `addr` a live region? This is what a caller uses to reject a primitive
    // or a voice naming an address that was never handed out (or was freed).
    bool region_exists(int64_t addr) const
    {
        return live_block(addr) != nullptr;
    }

    // Capacity of the region at `addr` in bytes, or RV_ERR_INVAL.
    int64_t region_size(int64_t addr) const
    {
        const rv_pcpool_block *block = live_block(addr);
        return block ? block->size : rv_pdk::RV_ERR_INVAL;
    }

    // Read-only view of a region's bytes, or nullptr when `addr` is not live.
    const uint8_t *region_data(int64_t addr) const
    {
        const rv_pcpool_block *block = live_block(addr);
        return block ? arena_.base() + block->offset : nullptr;
    }

    // The caller's metadata for the region, or nullptr when `addr` is not live.
    Meta *region_meta(int64_t addr)
    {
        rv_pcpool_block *block = live_block(addr);
        return block ? &block->meta : nullptr;
    }
    const Meta *region_meta(int64_t addr) const
    {
        const rv_pcpool_block *block = live_block(addr);
        return block ? &block->meta : nullptr;
    }

    int64_t capacity() const
    {
        return capacity_;
    }

private:
    // One span of the pool. `reserved` marks the head block that exists only to
    // keep address 0 out of circulation; it is never freed and never merged.
    struct rv_pcpool_block {
        int64_t offset = 0;
        int64_t size = 0;
        bool used = false;
        bool reserved = false;
        Meta meta{};
    };

    int64_t align_up(int64_t value) const
    {
        const int64_t remainder = value % alignment_;
        return remainder == 0 ? value : value + (alignment_ - remainder);
    }

    int64_t find_block(int64_t addr) const
    {
        for (size_t i = 0; i < blocks_.size(); ++i) {
            if (blocks_[i].offset == addr) {
                return static_cast<int64_t>(i);
            }
        }
        return -1;
    }

    // A block that exists, is allocated, and is not the reserved head — i.e.
    // one the caller could legitimately be naming.
    rv_pcpool_block *live_block(int64_t addr)
    {
        const int64_t at = find_block(addr);
        if (at < 0) {
            return nullptr;
        }
        rv_pcpool_block &block = blocks_[static_cast<size_t>(at)];
        return (block.used && !block.reserved) ? &block : nullptr;
    }
    const rv_pcpool_block *live_block(int64_t addr) const
    {
        const int64_t at = find_block(addr);
        if (at < 0) {
            return nullptr;
        }
        const rv_pcpool_block &block = blocks_[static_cast<size_t>(at)];
        return (block.used && !block.reserved) ? &block : nullptr;
    }

    rv_pcarena arena_;
    int64_t capacity_;
    int64_t alignment_;
    std::vector<rv_pcpool_block> blocks_; // offset-ordered, gapless
};

} // namespace rv_3dmppc
