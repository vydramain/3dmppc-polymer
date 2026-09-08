#include "rv_burner_compile_sources.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "rv_burner_common/rv_burner_process.hpp"
#include "pdklib/rv_disc_hash/rv_disc_hash.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The one artifact a disc project is required to produce. The loader looks for
// this exact name inside the archive, so the generated CMakeLists sets it (see
// cmake_project_text) and this phase checks it.
static constexpr const char *k_disc_module_name = "disc.so";

// Local hex formatting: pdklib ships the checksum as raw bytes, not text.
static std::string to_hex(const unsigned char *bytes, std::size_t n)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < n; ++i) {
        out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return out.str();
}

// Computes the disc code checksum over the freshly linked disc.so and writes
// it into rv_mppc_note_desc::magic in place. Must run once, right after the
// module is linked and before it is archived: the checksum covers .text/
// .rodata/.data only (see rv_disc_hash.hpp), so writing into the (excluded)
// version note here does not invalidate what was just computed.
static bool stamp_checksum(const fs::path &disc_module, std::string &error)
{
    std::ifstream in(disc_module, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "cannot open '" + disc_module.string() + "' to stamp its checksum";
        return false;
    }
    const std::streamsize length = in.tellg();
    in.seekg(0);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (length > 0 && !in.read(reinterpret_cast<char *>(bytes.data()), length)) {
        error = "cannot read '" + disc_module.string() + "' to stamp its checksum";
        return false;
    }
    in.close();

    unsigned char checksum[rv_pdklib::RV_DISC_HASH_BYTES];
    std::string hash_error;
    if (!rv_pdklib::rv_disc_hash_compute(bytes.data(), bytes.size(), checksum, hash_error)) {
        error = "cannot checksum '" + disc_module.string() + "': " + hash_error;
        return false;
    }

    std::size_t magic_offset = 0;
    std::string locate_error;
    if (!rv_pdklib::rv_disc_hash_magic_offset(bytes.data(), bytes.size(), magic_offset,
            locate_error)) {
        error = "'" + disc_module.string() +
            "' was built without RV_MPPC_DISC_VERSION_DEF, so there is no checksum "
            "field to stamp: " +
            locate_error;
        return false;
    }

    std::ofstream out(disc_module, std::ios::binary | std::ios::in | std::ios::out);
    if (!out) {
        error = "cannot reopen '" + disc_module.string() + "' to stamp its checksum";
        return false;
    }
    out.seekp(static_cast<std::streamoff>(magic_offset));
    out.write(reinterpret_cast<const char *>(checksum), sizeof(checksum));
    if (!out) {
        error = "cannot write the checksum into '" + disc_module.string() + "'";
        return false;
    }
    out.close();

    rv_pdklib::rv_fprintf(stderr, "disc code checksum: %s\n",
        to_hex(checksum, sizeof(checksum)).c_str());
    return true;
}

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

    // --- stamp the disc code checksum ---
    //
    // Right after the module is linked and before anything archives it: this
    // is the one place a checksum over the finished .text/.rodata/.data can be
    // computed and then written back into the same file.
    if (!stamp_checksum(disc_module, error)) {
        return 1;
    }

    return 0;
}
