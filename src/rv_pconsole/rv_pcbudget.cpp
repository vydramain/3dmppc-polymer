#include "rv_pcbudget.hpp"

#include <format>
#include <limits>

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

int rv_pcbudget_mul(const char *field, int64_t a, int64_t b, int64_t &out, std::string &error)
{
    if (a != 0 && b > std::numeric_limits<int64_t>::max() / a) {
        error = std::format("'{}' overflows: {} * {}", field, a, b);
        return RV_ERR_INVAL;
    }
    out = a * b;
    return RV_OK;
}

int rv_pcbudget_add(int64_t &total, const char *field, int64_t amount, std::string &error)
{
    if (amount < 0) {
        error = std::format("'{}' is negative ({})", field, amount);
        return RV_ERR_INVAL;
    }
    if (amount > std::numeric_limits<int64_t>::max() - total) {
        error = std::format("'{}' overflows the memory total", field);
        return RV_ERR_INVAL;
    }
    total += amount;
    return RV_OK;
}

} // namespace rv_3dmppc
