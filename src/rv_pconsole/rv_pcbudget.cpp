#include "rv_pcbudget.hpp"

#include <limits>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

bool rv_pcbudget_mul(const char *field, int64_t a, int64_t b, int64_t &out)
{
    if (a != 0 && b > std::numeric_limits<int64_t>::max() / a) {
        RV_LOG_ERR("pccheck", "'{}' overflows: {} * {}", field, a, b);
        return true;
    }
    out = a * b;
    return false;
}

bool rv_pcbudget_add(const char *field, int64_t &total, int64_t amount)
{
    if (amount > std::numeric_limits<int64_t>::max() - total) {
        RV_LOG_ERR("pccheck", "'{}' overflows the memory total", field);
        return true;
    }
    total += amount;
    return false;
}

} // namespace rv_3dmppc
