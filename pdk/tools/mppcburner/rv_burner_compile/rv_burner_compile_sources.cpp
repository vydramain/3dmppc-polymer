#include "rv_burner_compile_sources.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The one artifact a disc project is required to produce. The loader looks for
// this exact name inside the archive, so the generated CMakeLists sets it (see
// cmake_project_text) and this phase checks it.
static constexpr const char *k_disc_module_name = "disc.so";

} // namespace rv_pdktools

int rv_pdktools::compile_sources(
    const rv_burner_options &options,
    const fs::path &binary_dir,
    std::string &error)
{
    // --- build ---
    //
    // `cmake --build` rather than `ninja` by name: the generator is cmake's
    // business, and this line stays correct if it ever changes. It is also the
    // only portable spelling — a Windows port replaces what cmake drives
    // underneath, not this command.
    std::string build = "cmake --build " + shell_quote(binary_dir.string());
    if (options.jobs > 0) {
        build += " --parallel " + std::to_string(options.jobs);
    }

    std::string child_output;
    const int status = run_capture(build, child_output);
    if (status != 0) {
        dump_child_output(child_output);
        error = "compiling the disc failed (exit " + std::to_string(status) + ")";
        return 1;
    }

    // --- confirm the module exists ---
    //
    // A build can report success and still leave no module: a manifest whose
    // sources define no entry point, or a CMakeLists that renamed the target.
    // Catching it here names the real problem instead of letting the burn phase
    // fail on a missing file.
    std::error_code ec;
    const fs::path disc_module = binary_dir / k_disc_module_name;
    if (!fs::is_regular_file(disc_module, ec)) {
        dump_child_output(child_output);
        error = "the build reported success but produced no '" + disc_module.string() + "'";
        return 1;
    }

    return 0;
}
