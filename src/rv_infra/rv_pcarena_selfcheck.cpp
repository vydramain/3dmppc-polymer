#include <cstdint>
#include <cstring>

#include "pdk/rv_err.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pcarena.hpp"

#define RV_ARENA_CHECK(cond)                                       \
    do {                                                           \
        if (!(cond)) {                                             \
            RV_LOG_ERR("pcarena", "selfcheck failed: {}", #cond);  \
            return false;                                          \
        }                                                          \
    } while (0)

namespace rv_3dmppc {

bool rv_pcarena_selfcheck() {
    {
        rv_pcarena arena(1 << 20);  // 1 MiB
        RV_ARENA_CHECK(arena.valid());
        RV_ARENA_CHECK(arena.committed() == 0);

        RV_ARENA_CHECK(arena.commit(4096) == rv_pdk::RV_OK);
        uint8_t *p = arena.base();
        p[0] = 0xAB;
        p[4095] = 0xCD;
        RV_ARENA_CHECK(p[0] == 0xAB);
        RV_ARENA_CHECK(p[4095] == 0xCD);

        const int64_t committed_after_first = arena.committed();
        RV_ARENA_CHECK(arena.commit(1024) == rv_pdk::RV_OK);
        RV_ARENA_CHECK(arena.committed() == committed_after_first);

        const int64_t committed_before_bad = arena.committed();
        RV_ARENA_CHECK(arena.commit(arena.reserved() + 1) == rv_pdk::RV_ERR_INVAL);
        RV_ARENA_CHECK(arena.committed() == committed_before_bad);
    }

    {
        rv_pcarena arena(1 << 20);
        RV_ARENA_CHECK(arena.commit(4096) == rv_pdk::RV_OK);
        uint8_t zeroes[4096] = {};
        RV_ARENA_CHECK(std::memcmp(arena.base(), zeroes, sizeof(zeroes)) == 0);
    }

    return true;
}

}  // namespace rv_3dmppc

#undef RV_ARENA_CHECK
