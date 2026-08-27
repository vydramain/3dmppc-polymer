#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rv_pdklib
{

// The three value shapes of the dialect. The schema names one per key, the
// parser produces them, the binder consumes them.
enum class rv_manifest_value_kind {
	string,
	integer,
	array
};

struct rv_manifest_mvalue {
	rv_manifest_value_kind kind = rv_manifest_value_kind::string;
	std::string str;
	int64_t num = 0;
	std::vector<std::string> arr;
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
	}
	return "a value";
}

} // namespace rv_pdklib
