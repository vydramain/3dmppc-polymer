// The console's startup sequence. See main.cpp.
#pragma once

#include <filesystem>

namespace rv_3dmppc
{

// Directory holding the running executable, or an empty path when
// /proc/self/exe cannot be read. modes.toml and the default memory card live
// there.
std::filesystem::path rv_pboot_exe_dir();

int rv_pboot_run(int argc, char **argv);

} // namespace rv_3dmppc
