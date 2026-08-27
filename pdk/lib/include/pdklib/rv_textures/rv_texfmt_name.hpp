#pragma once

#include <cstring>

#include "pdk/cv/rv_texture.hpp"

namespace rv_pdklib
{

struct rv_texfmt_name {
	rv_pdk::rv_texfmt format;
	const char *text;

	static const rv_texfmt_name *by_format(rv_pdk::rv_texfmt format);
	static const rv_texfmt_name *by_text(const char *text);
};

inline constexpr rv_texfmt_name rv_texfmt_names[]{
	{ rv_pdk::rv_texfmt::RV_TEXFMT_IDX4, "idx4" },
	{ rv_pdk::rv_texfmt::RV_TEXFMT_IDX8, "idx8" },
	{ rv_pdk::rv_texfmt::RV_TEXFMT_DIRECT15, "direct15" }
};

inline const rv_texfmt_name *rv_texfmt_name::by_format(rv_pdk::rv_texfmt format)
{
	for (const rv_texfmt_name &row : rv_texfmt_names) {
		if (row.format == format) {
			return &row;
		}
	}
	return nullptr;
}

inline const rv_texfmt_name *rv_texfmt_name::by_text(const char *text)
{
	if (text == nullptr) {
		return nullptr;
	}
	for (const rv_texfmt_name &row : rv_texfmt_names) {
		if (std::strcmp(row.text, text) == 0) {
			return &row;
		}
	}
	return nullptr;
}

} // namespace rv_pdklib
