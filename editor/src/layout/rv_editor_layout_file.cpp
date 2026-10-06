// The layout file: where it lives, loading it, saving it atomically.

#include "layout/rv_editor_tile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Largest layout file read; anything bigger is not a layout.
constexpr auto layout_file_max_bytes = 1 << 20;

constexpr const char *env_xdg_config_home = "XDG_CONFIG_HOME"; // XDG config home directory environment variable
constexpr const char *env_home = "HOME";                       // home directory environment variable
constexpr std::string_view app_name = "3dmppc-editor";         // application name in config path
constexpr std::string_view layout_dir = "layout";              // layout subdirectory name
constexpr std::string_view config_dir = ".config";             // standard config directory name
constexpr std::string_view temp_file_suffix = ".tmp";          // temporary file suffix for atomic writes

} // namespace

std::filesystem::path rv_editor_layout_file_path()
{
    const char *xdg = std::getenv(env_xdg_config_home);
    if (xdg && xdg[0] != '\0') {
        std::filesystem::path p(xdg);
        if (p.is_absolute()) {
            return p / app_name / layout_dir;
        }
    }

    const char *home = std::getenv(env_home);
    if (home && home[0] != '\0') {
        std::filesystem::path p(home);
        if (p.is_absolute()) {
            return p / config_dir / app_name / layout_dir;
        }
    }

    return {};
}

int rv_editor_layout_load(const std::filesystem::path &path, rv_editor_pane_registry &panes,
    rv_editor_layout &layout)
{
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        return RV_ERR_IO;
    }
    if (sz > layout_file_max_bytes) {
        return RV_ERR_INVAL;
    }

    std::ifstream file(path, std::ios::binary);
    std::string text(sz, '\0');
    if (!file.read(text.data(), sz)) {
        return RV_ERR_IO;
    }

    return rv_editor_layout_read(text, panes, layout);
}

int rv_editor_layout_save(const std::filesystem::path &path, const rv_editor_pane_registry &panes,
    const rv_editor_layout &layout, std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return RV_ERR_IO;
    }

    std::filesystem::path tmp = path;
    tmp += temp_file_suffix;

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << rv_editor_layout_write(panes, layout);
    out.close();
    if (!out) {
        error = path.string() + ": write failed";
        return RV_ERR_IO;
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        error = path.string() + ": " + ec.message();
        return RV_ERR_IO;
    }

    return RV_OK;
}

} // namespace rv_editor
