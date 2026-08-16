#pragma once

#include <set>
#include <string>

#include "rv_burner_character.hpp"
#include "rv_burner_manifest_assigner.hpp"

namespace rv_pdktools
{

class rv_burner_manifest_parser
{
	rv_burner_manifest_assigner assigner_;
	const std::string &text_;
	std::size_t pos_ = 0;
	int line_ = 1;
	std::string section_;
	std::set<std::string> seen_sections_;
	std::string error_;

public:
	rv_burner_manifest_parser(const std::string &text)
		: assigner_(section_, error_)
		, text_(text)
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

	// --- grammar --------------------------------------------------------------

	bool parse_section();
	bool parse_assignment();
	bool parse_value(rv_burner_mvalue &out);
	bool parse_string(std::string &out);
	bool parse_array(std::vector<std::string> &out);
	bool parse_integer(int64_t &out);
	bool expect_line_end(const char *what);
};

} // namespace rv_pdktools
