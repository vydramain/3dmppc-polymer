// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — реализация пула видеопамяти.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/cv/rv_pcvram.hpp"

#include <cstring>

#include "pdk/rv_err.hpp"

namespace rv_3dmppc {

namespace {

// Every region starts on a 4-byte boundary. The pool holds 16-bit texels and
// 32-bit palette runs; an unaligned region would make the future texture
// sampler pay for byte-wise reads on every texel fetch.
constexpr int64_t RV_PCVRAM_ALIGN = 4;

// The pool's first bytes are never handed out, so no live region can ever have
// address 0. That is what lets rv_polygon::addr_texture == 0 read as "not set"
// — a zero-initialized primitive struct is the common case and must not
// accidentally name a real region.
constexpr int64_t RV_PCVRAM_RESERVED_HEAD = 16;

int64_t align_up(int64_t value, int64_t alignment) {
    const int64_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

}  // namespace

rv_pcvram::rv_pcvram(int64_t size) : pool_(static_cast<size_t>(size > 0 ? size : 0), 0) {
    const int64_t total = capacity();

    // The head block is `used` forever: allocation skips it, free() refuses it,
    // and coalescing stops at it.
    rv_pcvram_block head;
    head.offset = 0;
    head.size = total < RV_PCVRAM_RESERVED_HEAD ? total : RV_PCVRAM_RESERVED_HEAD;
    head.used = true;
    head.reserved = true;
    blocks_.push_back(head);

    if (total > head.size) {
        rv_pcvram_block body;
        body.offset = head.size;
        body.size = total - head.size;
        body.used = false;
        blocks_.push_back(body);
    }
}

int64_t rv_pcvram::find_block(int64_t addr) const {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].offset == addr) {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

int64_t rv_pcvram::malloc(int64_t size) {
    if (size <= 0) {
        return RV_ERR_INVAL;
    }

    const int64_t want = align_up(size, RV_PCVRAM_ALIGN);

    // First fit: walk the offset-ordered list and take the first free block that
    // is large enough. Best fit would waste less memory per call but leaves the
    // pool full of unusable slivers under the console's usage pattern (a disc
    // uploads its atlas once at load and rarely churns), and first fit keeps the
    // block list short, which every region_exists() check pays for.
    for (size_t i = 0; i < blocks_.size(); ++i) {
        rv_pcvram_block& block = blocks_[i];
        if (block.used || block.size < want) {
            continue;
        }

        const int64_t addr = block.offset;
        const int64_t leftover = block.size - want;

        block.size = want;
        block.used = true;
        block.written = false;
        block.width = 0;
        block.height = 0;

        if (leftover > 0) {
            rv_pcvram_block tail;
            tail.offset = addr + want;
            tail.size = leftover;
            tail.used = false;
            blocks_.insert(blocks_.begin() + static_cast<int64_t>(i) + 1, tail);
        }
        return addr;
    }

    return RV_ERR_NOMEM;
}

int64_t rv_pcvram::free(int64_t addr) {
    const int64_t at = find_block(addr);
    if (at < 0) {
        return RV_ERR_INVAL;
    }

    const size_t index = static_cast<size_t>(at);
    if (!blocks_[index].used || blocks_[index].reserved) {
        return RV_ERR_INVAL;  // double free, or an attempt at the reserved head
    }

    blocks_[index].used = false;
    blocks_[index].written = false;

    // Coalesce with the neighbours. Without this the pool degrades into a chain
    // of free slivers that no future allocation fits into even though the total
    // free byte count is plenty — the classic external-fragmentation death of a
    // long-running allocator. Merging on release keeps the invariant "no two
    // adjacent free blocks", so the largest free block is always as large as the
    // contiguous free space actually allows.
    if (index + 1 < blocks_.size() && !blocks_[index + 1].used) {
        blocks_[index].size += blocks_[index + 1].size;
        blocks_.erase(blocks_.begin() + static_cast<int64_t>(index) + 1);
    }
    if (index > 0 && !blocks_[index - 1].used) {
        blocks_[index - 1].size += blocks_[index].size;
        blocks_.erase(blocks_.begin() + static_cast<int64_t>(index));
    }

    return RV_OK;
}

int64_t rv_pcvram::write(int64_t addr, const rv_texture& texture) {
    const int64_t at = find_block(addr);
    if (at < 0) {
        return RV_ERR_INVAL;
    }

    rv_pcvram_block& block = blocks_[static_cast<size_t>(at)];
    if (!block.used || block.reserved) {
        return RV_ERR_INVAL;
    }

    const int64_t bytes = static_cast<int64_t>(texture.size);
    if (bytes < 0 || bytes > block.size) {
        return RV_ERR_INVAL;
    }
    if (bytes > 0 && texture.data == nullptr) {
        return RV_ERR_INVAL;
    }

    if (bytes > 0) {
        // The contract promises the bytes are copied during the call, so the
        // disc may release its own buffer the moment this returns.
        std::memcpy(pool_.data() + block.offset, texture.data, static_cast<size_t>(bytes));
    }

    block.written = true;
    block.format = texture.format;
    block.width = static_cast<int64_t>(texture.width);
    block.height = static_cast<int64_t>(texture.height);

    return RV_OK;
}

bool rv_pcvram::region_exists(int64_t addr) const {
    const int64_t at = find_block(addr);
    if (at < 0) {
        return false;
    }
    const rv_pcvram_block& block = blocks_[static_cast<size_t>(at)];
    return block.used && !block.reserved;
}

int64_t rv_pcvram::region_format(int64_t addr) const {
    const int64_t at = find_block(addr);
    if (at < 0) {
        return RV_ERR_INVAL;
    }
    const rv_pcvram_block& block = blocks_[static_cast<size_t>(at)];
    if (!block.used || block.reserved) {
        return RV_ERR_INVAL;
    }
    if (!block.written) {
        return RV_ERR_NOENT;
    }
    return static_cast<int64_t>(block.format);
}

const uint8_t* rv_pcvram::region_data(int64_t addr) const {
    const int64_t at = find_block(addr);
    if (at < 0) {
        return nullptr;
    }
    const rv_pcvram_block& block = blocks_[static_cast<size_t>(at)];
    if (!block.used || block.reserved) {
        return nullptr;
    }
    return pool_.data() + block.offset;
}

}  // namespace rv_3dmppc
