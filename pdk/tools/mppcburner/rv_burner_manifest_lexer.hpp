#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rv_burner_manifest_token.hpp"

namespace rv_pdktools
{

// Stage 1: characters → tokens. Whitespace and comments are thrown away, a
// newline is kept because the grammar is line-oriented. Total by design —
// anything it cannot read comes out as an INVALID token carrying the message,
// so the scanner never talks to the error handler.
class rv_burner_manifest_lexer
{
	const std::string &text_;
	std::size_t pos_ = 0;
	int line_ = 1;

public:
	explicit rv_burner_manifest_lexer(const std::string &text)
		: text_(text)
	{
	}

	// One token per call; END_OF_FILE for ever after.
	rv_burner_token next();

private:
	bool eof() const;
	char peek(std::size_t ahead = 0) const;
	char get();

	// Spaces, tabs and stray carriage returns — never a newline.
	void skip_inline();
	void skip_comment();

	rv_burner_token lex_ident();
	rv_burner_token lex_string();
	rv_burner_token lex_integer();
};

// Drains a lexer into a vector that always ends with exactly one END_OF_FILE,
// which is what lets the parser peek without bounds checks.
std::vector<rv_burner_token> rv_burner_manifest_lex(const std::string &text);

} // namespace rv_pdktools
