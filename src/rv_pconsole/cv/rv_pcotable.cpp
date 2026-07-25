// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — реализация ordering table.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/cv/rv_pcotable.hpp"

namespace rv_3dmppc {

rv_pcotable::rv_pcotable(int64_t bucket_count, int32_t depth_min, int32_t depth_max)
    : bucket_count_(bucket_count > 0 ? bucket_count : 1),
      depth_min_(depth_min),
      depth_max_(depth_max > depth_min ? depth_max : depth_min),
      head_(static_cast<size_t>(bucket_count_), -1),
      tail_(static_cast<size_t>(bucket_count_), -1) {}

void rv_pcotable::reset() {
    for (size_t i = 0; i < head_.size(); ++i) {
        head_[i] = -1;
        tail_[i] = -1;
    }
}

int64_t rv_pcotable::bucket_of(int32_t depth) const {
    // THEOREM: linear quantization with SATURATION — bucket =
    // floor((depth - depth_min) * bucket_count / (depth_max - depth_min + 1)),
    // with everything below depth_min pinned to bucket 0 and everything above
    // depth_max pinned to the last bucket. The contract (rv_primitives.hpp)
    // says out-of-range depths "clamp to the nearest bucket", so the mapping
    // must saturate and NOT wrap: a wrap would teleport a far background in
    // front of the HUD the moment a disc's camera overshot its depth window.
    //
    // The arithmetic runs in int64 because depth_max - depth_min spans the full
    // int32 range on the reference console and the numerator multiplies that
    // span by the bucket count.
    if (depth <= depth_min_) {
        return 0;
    }
    if (depth >= depth_max_) {
        return bucket_count_ - 1;
    }

    const int64_t span = static_cast<int64_t>(depth_max_) - static_cast<int64_t>(depth_min_) + 1;
    const int64_t offset = static_cast<int64_t>(depth) - static_cast<int64_t>(depth_min_);
    const int64_t bucket = offset * bucket_count_ / span;

    if (bucket < 0) {
        return 0;
    }
    if (bucket >= bucket_count_) {
        return bucket_count_ - 1;
    }
    return bucket;
}

void rv_pcotable::insert(int32_t depth, int32_t primitive_index) {
    if (primitive_index < 0) {
        return;
    }

    const size_t node = static_cast<size_t>(primitive_index);
    if (node >= next_.size()) {
        next_.resize(node + 1, -1);
    }
    next_[node] = -1;

    const size_t bucket = static_cast<size_t>(bucket_of(depth));

    // Append at the TAIL, not the head. A head insertion would be one store
    // cheaper but would reverse the submission order inside a bucket, and the
    // contract is explicit: "at equal depth the submission order is kept (first
    // put = drawn first = behind)". Keeping a tail pointer buys that guarantee
    // for one extra int per bucket and keeps the append O(1).
    if (head_[bucket] < 0) {
        head_[bucket] = primitive_index;
    } else {
        next_[static_cast<size_t>(tail_[bucket])] = primitive_index;
    }
    tail_[bucket] = primitive_index;
}

}  // namespace rv_3dmppc
