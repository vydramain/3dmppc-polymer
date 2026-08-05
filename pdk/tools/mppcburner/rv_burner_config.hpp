#pragma once

#include <string>

namespace rv_pdktools
{

struct rv_burn_options {
	std::string disc_dir;   // directory holding disc.toml
	std::string output;     // -o, the .mppcdisc to write
	std::string pdk_dir;    // --pdk, defaults to RV_BURNER_DEFAULT_PDK
	std::string pdklib_dir; // --pdklib, defaults to RV_BURNER_DEFAULT_PDKLIB
	std::string baker;      // --baker, empty means "find mppcbaker yourself"
	std::string build_dir;  // --keep-build=PATH, empty means <disc_dir>/.mppcburn
	int jobs = 0;           // --jobs, 0 means "let cmake decide"
	bool keep_build = false;
};

struct rv_insp_options {
	std::string &archive_path;
};

} // namespace rv_pdktools
