#pragma once

#include <cstdint>
#include <string>

namespace rv_pdklib
{

// What the lexer hands the parser: one kind per indivisible piece of a
// manifest. Whitespace and comments are dropped by the lexer and never appear
// here. INVALID is what makes the lexer total — every input produces a token
// stream, so the lexer never needs to reach for the error handler.
enum class rv_manifest_token_kind : int {
	IDENT, // bare word — a section name or a key
	EQUALS, // '='
	LBRACKET, // '[' — opens a section header or an array
	RBRACKET, // ']'
	COMMA, // ',' — separates array elements
	STRING, // "..." — quotes stripped, escapes already decoded
	INTEGER, // decimal, optional leading '+' or '-'
	NEWLINE, // the grammar is line-oriented: one value per line
	END_OF_FILE, // NOT `EOF`: <cstdio> defines that name as a macro
	INVALID // NOT `ERROR`: <wingdi.h> defines that name as a macro
};

// `text` carries the spelling of an IDENT, the decoded contents of a STRING, or
// the ready message of an INVALID; `num` only ever holds an INTEGER.
struct rv_manifest_token {
	rv_manifest_token_kind kind = rv_manifest_token_kind::END_OF_FILE;
	std::string text;
	int64_t num = 0;
	int line = 0;
};

// How a token is named inside a diagnostic.
inline std::string rv_manifest_token_spelling(const rv_manifest_token &t)
{
	switch (t.kind) {
	case rv_manifest_token_kind::IDENT:
	case rv_manifest_token_kind::INVALID:
		return "'" + t.text + "'";
	case rv_manifest_token_kind::EQUALS:
		return "'='";
	case rv_manifest_token_kind::LBRACKET:
		return "'['";
	case rv_manifest_token_kind::RBRACKET:
		return "']'";
	case rv_manifest_token_kind::COMMA:
		return "','";
	case rv_manifest_token_kind::STRING:
		return "a string";
	case rv_manifest_token_kind::INTEGER:
		return "an integer";
	case rv_manifest_token_kind::NEWLINE:
		return "end of line";
	case rv_manifest_token_kind::END_OF_FILE:
		return "end of file";
	}
	return "something unreadable";
}

} // namespace rv_pdklib
