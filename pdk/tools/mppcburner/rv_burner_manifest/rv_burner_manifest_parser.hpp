#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_manifest_token.hpp"
#include "rv_burner_manifest_tree.hpp"
#include "rv_burner_manifest_value.hpp"

namespace rv_pdktools
{

// Stage 2: tokens → document tree. Syntax only — it knows that a line is either
// a section header or `key = value`, and nothing about which sections exist. On
// an error it reports and resynchronises on the next plausible line, so one run
// names every broken line instead of only the first.
class rv_burner_manifest_parser
{
	const std::vector<rv_burner_token> &tokens_;
	rv_burner_manifest_failer &failer_;
	std::size_t pos_ = 0;
	rv_burner_tree tree_;

public:
	rv_burner_manifest_parser(const std::vector<rv_burner_token> &tokens,
		rv_burner_manifest_failer &failer)
		: tokens_(tokens)
		, failer_(failer)
	{
	}

	// The tree, however much of it survived. Ask the failer whether it is sound.
	rv_burner_tree run();

private:
	// --- cursor ---------------------------------------------------------------

	const rv_burner_token &peek() const;
	const rv_burner_token &get();
	bool at(rv_burner_token_kind kind) const;

	// --- grammar --------------------------------------------------------------

	bool parse_section();
	bool parse_assignment();
	bool parse_value(rv_burner_mvalue &out);
	bool parse_array(rv_burner_mvalue &out);
	bool expect_line_end(const char *what);

	// --- recovery -------------------------------------------------------------

	// Drops lines until one looks like a statement again. Without this an
	// unterminated array turns into one complaint per element.
	void recover();
	bool starts_statement() const;

	void open_section(std::string name, int line, bool poisoned);
	rv_burner_tree_section &current_section();
};

} // namespace rv_pdktools
