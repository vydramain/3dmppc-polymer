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

} // namespace

std::filesystem::path rv_editor_layout_file_path()
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        std::filesystem::path p(xdg);
        if (p.is_absolute()) {
            return p / "3dmppc-editor" / "layout";
        }
    }

    const char *home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        std::filesystem::path p(home);
        if (p.is_absolute()) {
            return p / ".config" / "3dmppc-editor" / "layout";
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
    tmp += ".tmp";

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
