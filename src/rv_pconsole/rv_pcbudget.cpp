#include "rv_pcbudget.hpp"

#include <format>
#include <limits>

namespace rv_3dmppc
{

bool rv_pcbudget_mul(rv_pcbudget_cost &cost, const char *field, int64_t a, int64_t b, int64_t &out)
{
    if (a != 0 && b > std::numeric_limits<int64_t>::max() / a) {
        cost.reason = std::format("'{}' overflows: {} * {}", field, a, b);
        return true;
    }
    out = a * b;
    return false;
}

bool rv_pcbudget_add(rv_pcbudget_cost &cost, const char *field, int64_t amount)
{
    if (amount < 0) {
        cost.reason = std::format("'{}' is negative ({})", field, amount);
        return true;
    }
    if (amount > std::numeric_limits<int64_t>::max() - cost.bytes) {
        cost.reason = std::format("'{}' overflows the memory total", field);
        return true;
    }
    cost.bytes += amount;
    return false;
}

} // namespace rv_3dmppc
