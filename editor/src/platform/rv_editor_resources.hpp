#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace rv_editor
{

// The delivery is moved as a whole, so resources are found beside the executable, not at paths
// baked at build time; Linux tells a process its own executable through /proc/self/exe.
inline constexpr std::string_view rv_editor_self_exe_link = "/proc/self/exe";
inline constexpr std::string_view rv_editor_resource_dir_name = "editor";

// Directory of the running executable; empty on failure, reported on stderr at each failed lookup.
inline std::filesystem::path rv_editor_exe_dir()
{
    const std::string link(rv_editor_self_exe_link);
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink(link, ec);
    if (ec) {
        std::fprintf(stderr, "3dmppc-editor: cannot read %s: %s\n", link.c_str(), ec.message().c_str());
        return {};
    }
    return self.parent_path();
}

// <dir of the executable>/editor/<relative>; empty when the executable directory is unknown;
// a font load then fails and names the empty path, the icons are skipped.
inline std::filesystem::path rv_editor_resource_path(std::string_view relative)
{
    const std::filesystem::path dir = rv_editor_exe_dir();
    if (dir.empty()) {
        return {};
    }
    return dir / std::string(rv_editor_resource_dir_name) / std::string(relative);
}

} // namespace rv_editor
