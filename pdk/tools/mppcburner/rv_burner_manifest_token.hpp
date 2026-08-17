#pragma once

namespace rv_pdktools
{

// What the lexer hands the parser: one kind per indivisible piece of a
// manifest. Whitespace and comments are dropped by the lexer and never appear
// here. INVALID is what makes the lexer total — every input produces a token
// stream, so the lexer never needs to reach for the error handler.
enum class rv_burner_manifest_token : int {
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

} // namespace rv_pdktools
