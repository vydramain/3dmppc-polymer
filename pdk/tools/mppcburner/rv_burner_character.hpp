#pragma once

#include <string>
#include <vector>

namespace rv_pdktools
{

enum class rv_burner_value_kind {
	string,
	integer,
	array
};

struct rv_burner_mvalue {
	rv_burner_value_kind kind = rv_burner_value_kind::string;
	std::string str;
	int64_t num = 0;
	std::vector<std::string> arr;
	int line = 0; // where the value STARTED, which is where the author must look
};

inline std::string_view rv_burner_kind_name(rv_burner_value_kind k)
{
	switch (k) {
	case rv_burner_value_kind::string:
		return "a string";
	case rv_burner_value_kind::integer:
		return "an integer";
	case rv_burner_value_kind::array:
		return "an array of strings";
	}
	return "a value";
}

bool rv_burner_is_ident(char c);
bool rv_burner_is_digit(char c);

} // namespace rv_pdktools
