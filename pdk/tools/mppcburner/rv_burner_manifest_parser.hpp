#pragma once

#include <set>
#include <string>

#include "rv_manifest.hpp"

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

class rv_burner_manifest_parser
{
	const std::string &text_;
	rv_manifest &manifest_;
	std::size_t pos_ = 0;
	int line_ = 1;
	std::string section_;
	std::set<std::string> seen_sections_;
	std::set<std::string> seen_keys_;
	std::string error_;

public:
	rv_burner_manifest_parser(const std::string &text, rv_manifest &out)
		: text_(text)
		, manifest_(out)
	{
	}

	bool run(std::string &error);

private:
	// --- cursor ---------------------------------------------------------------

	bool eof() const;
	char peek() const;
	char get();

	// Spaces, tabs and stray carriage returns — never a newline.
	void skip_inline();
	void skip_comment();

	// Everything that may separate two meaningful tokens: whitespace, blank
	// lines and full-line comments. Also used INSIDE an array, which is what
	// makes a multi-line array work without a special case.
	void skip_gaps();
	bool fail(int line, const std::string &message);

	// --- grammar --------------------------------------------------------------

	bool parse_section();
	bool parse_assignment();
	bool parse_value(rv_burner_mvalue &out);
	bool parse_string(std::string &out);
	bool parse_array(std::vector<std::string> &out);
	bool parse_integer(int64_t &out);
	bool expect_line_end(const char *what);

	// --- binding a value to a field -------------------------------------------
	bool wrong_type(const std::string &key, const rv_burner_mvalue &v, rv_burner_value_kind want);
	bool assign(const std::string &key, const rv_burner_mvalue &value, int key_line);
};

} // namespace rv_pdktools
