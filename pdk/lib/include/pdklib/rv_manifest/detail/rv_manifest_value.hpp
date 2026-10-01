#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rv_pdklib
{

// The value shapes of the dialect. The schema names one per key, the parser
// produces them, the binder consumes them. disc.toml uses the first three; real
// and numbers are for the other files in the dialect.
enum class rv_manifest_value_kind {
    string,
    integer,
    array,
    real,
    numbers
};

struct rv_manifest_mvalue {
    rv_manifest_value_kind kind = rv_manifest_value_kind::string;
    std::string str;
    int64_t num = 0;
    double real = 0.0;
    std::vector<std::string> arr;
    std::vector<double> nums; // an array of numbers; integers in it are exact up to 2^53
    int line = 0; // where the value STARTED, which is where the author must look
};

inline std::string_view rv_manifest_kind_name(rv_manifest_value_kind k)
{
    switch (k) {
    case rv_manifest_value_kind::string:
        return "a string";
    case rv_manifest_value_kind::integer:
        return "an integer";
    case rv_manifest_value_kind::array:
        return "an array of strings";
    case rv_manifest_value_kind::real:
        return "a number with a fraction";
    case rv_manifest_value_kind::numbers:
        return "an array of numbers";
    }
    return "a value";
}

} // namespace rv_pdklib
