#include "rv_burner_compile_sources.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "rv_burner_common_helpers.hpp"
#include "rv_burner_print.hpp"

namespace fs = std::filesystem;

int rv_pdktools::rv_burner_compile_sources(
	const rv_burner_options &options,
	const fs::path &binary_dir,
	std::size_t source_count,
	std::string &error)
{
	std::string build = "cmake --build " + shell_quote(binary_dir.string());
	if (options.jobs > 0) {
		build += " --parallel " + std::to_string(options.jobs);
	}

	std::string child_output;
	const int status = run_capture(build, child_output);
	if (status != 0) {
		// The compiler's own diagnostic, verbatim. Nothing this tool could say
		// about a broken disc is more useful than what the compiler already
		// said about it.
		dump_child_output(child_output);
		error = "compiling the disc failed (exit " + std::to_string(status) + ")";
		return 1;
	}

	std::error_code ec;
	const fs::path disc_so = binary_dir / "disc.so";
	if (!fs::is_regular_file(disc_so, ec)) {
		dump_child_output(child_output);
		error = "the build reported success but produced no '" + disc_so.string() + "'";
		return 1;
	}

	rv_burner_print_step(2, "compile", std::to_string(source_count) + " source(s) -> disc.so");
	return 0;
}
