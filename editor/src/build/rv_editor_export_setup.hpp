#pragma once

#include <cstdlib>
#include <filesystem>

namespace rv_editor
{

// `emscripten_dir` is the directory rv_burner_emscripten_dir returns; the burner writes the marker last.
inline bool rv_editor_web_support_installed(const std::filesystem::path &emscripten_dir)
{
    std::error_code ec;
    return std::filesystem::exists(emscripten_dir / ".mppcburner-ready", ec);
}

// Where the burner installs Emscripten; empty when neither XDG_DATA_HOME nor HOME is set.
// shortcut: the Emscripten version is copied from rv_burner_setup.cpp; move it to a shared pdkkit header when it next
// changes
inline std::filesystem::path rv_editor_emscripten_dir()
{
    const char *xdg = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    std::filesystem::path root;
    if (xdg != nullptr && *xdg != '\0') {
        root = xdg;
    } else if (home != nullptr && *home != '\0') {
        root = std::filesystem::path(home) / ".local/share";
    } else {
        return {};
    }
    return root / "3dmppc" / "emscripten-6.0.10";
}

} // namespace rv_editor
