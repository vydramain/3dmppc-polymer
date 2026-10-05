// The view file: the code text size, the Game scale and the workspace, one line each.

#include "prefs/rv_editor_prefs.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "pdk/rv_err.h"

#include "layout/rv_editor_tile.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

namespace
{

const std::string rv_editor_prefs_magic = std::string("3dmppc-editor-view ") + rv_pdklib::rv_version_str;

constexpr rv_editor_game_scale rv_editor_game_scales[] = { rv_editor_game_scale::fit, rv_editor_game_scale::integer,
    rv_editor_game_scale::x1, rv_editor_game_scale::x2, rv_editor_game_scale::x3 };

} // namespace

const char *rv_editor_game_scale_name(rv_editor_game_scale scale)
{
    switch (scale) {
        case rv_editor_game_scale::fit: return "fit";
        case rv_editor_game_scale::integer: return "integer";
        case rv_editor_game_scale::x1: return "1x";
        case rv_editor_game_scale::x2: return "2x";
        case rv_editor_game_scale::x3: return "3x";
    }
    return "fit";
}

int rv_editor_game_scale_parse(std::string_view name, rv_editor_game_scale &scale)
{
    for (const rv_editor_game_scale s : rv_editor_game_scales) {
        if (name == rv_editor_game_scale_name(s)) {
            scale = s;
            return RV_OK;
        }
    }
    return RV_ERR_INVAL;
}

std::string rv_editor_prefs_write(const rv_editor_prefs &prefs)
{
    std::string out(rv_editor_prefs_magic);
    out += "\ncode-font ";
    out += rv_editor_code_size_name(prefs.code_size);
    out += "\ngame-scale ";
    out += rv_editor_game_scale_name(prefs.game_scale);
    out += "\nworkspace ";
    out += prefs.workspace;
    out += "\nui-scale ";
    if (prefs.ui_scale == 2.0f) {
        out += "2";
    } else if (prefs.ui_scale == 1.5f) {
        out += "1.5";
    } else {
        out += "1";
    }
    out += "\n";
    return out;
}

rv_editor_prefs rv_editor_prefs_read(std::string_view text)
{
    rv_editor_prefs prefs;
    std::istringstream in{ std::string(text) };
    std::string line;
    if (!std::getline(in, line) || line != rv_editor_prefs_magic) {
        return prefs;
    }
    while (std::getline(in, line)) {
        const size_t space = line.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, space);
        const std::string value = line.substr(space + 1);
        if (key == "code-font") {
            (void)rv_editor_code_size_parse(value.c_str(), prefs.code_size);
        } else if (key == "game-scale") {
            (void)rv_editor_game_scale_parse(value, prefs.game_scale);
        } else if (key == "workspace") {
            prefs.workspace = value;
        } else if (key == "ui-scale") {
            if (value == "3" || value == "2") {
                prefs.ui_scale = 2.0f;
            } else if (value == "1.5") {
                prefs.ui_scale = 1.5f;
            } else {
                prefs.ui_scale = 1.0f;
            }
        }
    }
    return prefs;
}

std::filesystem::path rv_editor_prefs_file_path()
{
    const std::filesystem::path layout = rv_editor_layout_file_path();
    return layout.empty() ? layout : layout.parent_path() / "view";
}

rv_editor_prefs rv_editor_prefs_load(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (path.empty() || ec || size > 4096) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    std::string text(size, '\0');
    if (!file.read(text.data(), static_cast<std::streamsize>(size))) {
        return {};
    }
    return rv_editor_prefs_read(text);
}

int rv_editor_prefs_save(const std::filesystem::path &path, const rv_editor_prefs &prefs, std::string &error)
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
    out << rv_editor_prefs_write(prefs);
    out.close();
    if (!out) {
        error = path.string() + ": write failed";
        return RV_ERR_IO;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        error = path.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace rv_editor
