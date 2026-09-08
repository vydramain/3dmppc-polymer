#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rv_manifest_failer.hpp"
#include "rv_manifest_token.hpp"
#include "rv_manifest_tree.hpp"
#include "rv_manifest_value.hpp"

namespace rv_pdklib
{

// Stage 2: tokens → document tree. Syntax only — it knows that a line is either
// a section header or `key = value`, and nothing about which sections exist. On
// an error it reports and resynchronises on the next plausible line, so one run
// names every broken line instead of only the first.
class rv_manifest_parser
{
	const std::vector<rv_manifest_token> &tokens_;
	rv_manifest_failer &failer_;
	std::size_t pos_ = 0;
	rv_manifest_tree tree_;

public:
	rv_manifest_parser(const std::vector<rv_manifest_token> &tokens,
		rv_manifest_failer &failer)
		: tokens_(tokens)
		, failer_(failer)
	{
	}

	// The tree, however much of it survived. Ask the failer whether it is sound.
	rv_manifest_tree run();

private:
	// --- cursor ---------------------------------------------------------------

	const rv_manifest_token &peek() const;
	const rv_manifest_token &get();
	bool at(rv_manifest_token_kind kind) const;

	// --- grammar --------------------------------------------------------------

	bool parse_section();
	bool parse_assignment();
	bool parse_value(rv_manifest_mvalue &out);
	bool parse_array(rv_manifest_mvalue &out);
	bool expect_line_end(const char *what);

	// --- recovery -------------------------------------------------------------

	// Drops lines until one looks like a statement again. Without this an
	// unterminated array turns into one complaint per element.
	void recover();
	bool starts_statement() const;

	void open_section(std::string name, int line, bool poisoned);
	rv_manifest_tree_section &current_section();
};

} // namespace rv_pdklib
