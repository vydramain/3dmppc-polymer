// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 5 — контроллер привода: таблица хендлов поверх носителя.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's rv_cd implementation: the DRIVE, not the disc. It owns the
// contract's semantics — legal names, stable handles, which rv_err a situation
// deserves — and delegates every actual byte to an rv_pcmedium (rv_pcmedium.hpp),
// so it has no idea whether the disc it is reading is a directory today or a
// `.mppcdisc` archive after stage 10.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "pdk/cd/rv_cd.hpp"
#include "rv_pconsole/cd/rv_pcmedium.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {
class rv_pccd : public rv_cd {
   private:
    // NEUROSLOP-BEGIN (claude-opus-5)
    rv_pccd_conf conf_;

    // What is inserted. Held by pointer purely so the medium KIND can change
    // without this class changing (PATTERN: strategy); it is never null, an
    // empty drive is a mounted-less medium rather than a missing one.
    std::unique_ptr<rv_pcmedium> medium_;

    // PATTERN: handle table — a handle is simply an index into `resnames_`, and
    // `by_name_` makes the resolution idempotent. Two properties fall out, and
    // both are contract requirements rather than conveniences:
    //   * the same name always yields the same handle, because a name resolved
    //     once is found in `by_name_` and its index returned again;
    //   * a handle needs no release and can never dangle, because the table only
    //     ever grows — nothing is erased, reordered or reused for a later name.
    // The cost is one std::string per DISTINCT name ever opened, which is
    // bounded by the disc's asset set and is why no eviction policy is needed.
    std::vector<std::string> resnames_;
    std::unordered_map<std::string, int64_t> by_name_;
    // NEUROSLOP-END

   public:
    // NEUROSLOP-BEGIN (claude-opus-5)
    explicit rv_pccd(const rv_pccd_conf& conf);
    // NEUROSLOP-END
    ~rv_pccd() override = default;

    rv_pccd(const rv_pccd&) = delete;
    rv_pccd& operator=(const rv_pccd&) = delete;

    int64_t asset_open(const char* resname) override;

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void* baddr, int64_t baddr_size) override;

   private:
    // NEUROSLOP-BEGIN (claude-opus-5)
    // The upper bound on distinct names one disc may resolve. It exists so that
    // RV_ERR_NOMEM is a real, testable answer ("the resource table cannot grow")
    // instead of a code that only ever appears when the host is already dying.
    static constexpr int64_t kResourceTableMax = 4096;

    // Name behind a handle, or nullptr when the handle was never issued.
    const char* handle_name(int64_t handle) const;
    // NEUROSLOP-END
};

}  // namespace rv_3dmppc
