#pragma once

#include <string_view>

namespace rv_3dmppc
{

// Parses the embedded console texts (rv_console_texts.toml) into the table.
// RV_OK, or RV_ERR_INVAL with the parser's reason logged.
int rv_pctexts_load();

// Text by id ("boot.starting"). An unknown id returns itself and warns once.
// The pointer lives until the next rv_pctexts_load(). Main thread only.
const char *rv_pctext(std::string_view id);

} // namespace rv_3dmppc
