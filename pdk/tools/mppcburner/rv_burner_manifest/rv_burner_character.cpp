#include "rv_burner_character.hpp"

#include <cctype>

bool rv_pdktools::rv_burner_is_ident(char c)
{
	const unsigned char u = static_cast<unsigned char>(c);
	return std::isalnum(u) != 0 || c == '_' || c == '-';
}

bool rv_pdktools::rv_burner_is_digit(char c)
{
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}
