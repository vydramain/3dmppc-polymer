#include "rv_pcbudget.hpp"

#include <format>
#include <limits>

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

int rv_pcbudget_mul(rv_pcbudget_cost &cost, const char *field, int64_t a, int64_t b, int64_t &out)
{
    if (a != 0 && b > std::numeric_limits<int64_t>::max() / a) {
        cost.reason = std::format("'{}' overflows: {} * {}", field, a, b);
        return RV_ERR_INVAL;
    }
    out = a * b;
    return RV_OK;
}

int rv_pcbudget_add(rv_pcbudget_cost &cost, const char *field, int64_t amount)
{
    if (amount < 0) {
        cost.reason = std::format("'{}' is negative ({})", field, amount);
        return RV_ERR_INVAL;
    }
    if (amount > std::numeric_limits<int64_t>::max() - cost.bytes) {
        cost.reason = std::format("'{}' overflows the memory total", field);
        return RV_ERR_INVAL;
    }
    cost.bytes += amount;
    return RV_OK;
}

} // namespace rv_3dmppc
