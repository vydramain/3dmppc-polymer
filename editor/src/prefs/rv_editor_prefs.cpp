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

// View preferences file header: magic string prefix with PDK version
constexpr std::string_view prefs_header_prefix = "3dmppc-editor-view ";

// Preference keys for code font size, game scale, workspace, and UI scale
constexpr std::string_view prefs_key_code_font = "code-font";
constexpr std::string_view prefs_key_game_scale = "game-scale";
constexpr std::string_view prefs_key_workspace = "workspace";
constexpr std::string_view prefs_key_ui_scale = "ui-scale";

// File format: key-value separator and line ending
constexpr std::string_view key_value_sep = " ";
constexpr char line_end = '\n';

// Game scale names as stored in view file and shown in menu
constexpr std::string_view game_scale_fit = "fit";
constexpr std::string_view game_scale_integer = "integer";
constexpr std::string_view game_scale_1x = "1x";
constexpr std::string_view game_scale_2x = "2x";
constexpr std::string_view game_scale_3x = "3x";

// UI scale numeric values and their string representations
constexpr float ui_scale_large = 2.0f;
constexpr float ui_scale_medium = 1.5f;
constexpr float ui_scale_default = 1.0f;
constexpr std::string_view ui_scale_large_str = "2";
constexpr std::string_view ui_scale_medium_str = "1.5";
constexpr std::string_view ui_scale_default_str = "1";
constexpr std::string_view ui_scale_legacy_large_str = "3"; // legacy: "3" also maps to large scale

// Temporary file extension for atomic writes
constexpr std::string_view temp_file_extension = ".tmp";

// View preferences file name, next to layout file
constexpr std::string_view prefs_file_name = "view";

// Maximum prefs file size: 4 KiB
constexpr size_t prefs_max_file_size = 4096;

const std::string rv_editor_prefs_magic = std::string(prefs_header_prefix) + rv_pdklib::rv_version_str;

constexpr rv_editor_game_scale rv_editor_game_scales[] = { rv_editor_game_scale::fit, rv_editor_game_scale::integer,
    rv_editor_game_scale::x1, rv_editor_game_scale::x2, rv_editor_game_scale::x3 };

} // namespace

const char *rv_editor_game_scale_name(rv_editor_game_scale scale)
{
    switch (scale) {
    case rv_editor_game_scale::fit:
        return game_scale_fit.data();
    case rv_editor_game_scale::integer:
        return game_scale_integer.data();
    case rv_editor_game_scale::x1:
        return game_scale_1x.data();
    case rv_editor_game_scale::x2:
        return game_scale_2x.data();
    case rv_editor_game_scale::x3:
        return game_scale_3x.data();
    }
    return game_scale_fit.data();
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
    out += line_end;
    out += prefs_key_code_font;
    out += key_value_sep;
    out += rv_editor_code_size_name(prefs.code_size);
    out += line_end;
    out += prefs_key_game_scale;
    out += key_value_sep;
    out += rv_editor_game_scale_name(prefs.game_scale);
    out += line_end;
    out += prefs_key_workspace;
    out += key_value_sep;
    out += prefs.workspace;
    out += line_end;
    out += prefs_key_ui_scale;
    out += key_value_sep;
    if (prefs.ui_scale == ui_scale_large) {
        out += ui_scale_large_str;
    } else if (prefs.ui_scale == ui_scale_medium) {
        out += ui_scale_medium_str;
    } else {
        out += ui_scale_default_str;
    }
    out += line_end;
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
        const size_t space = line.find(key_value_sep);
        if (space == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, space);
        const std::string value = line.substr(space + key_value_sep.size());
        if (key == prefs_key_code_font) {
            (void)rv_editor_code_size_parse(value.c_str(), prefs.code_size);
        } else if (key == prefs_key_game_scale) {
            (void)rv_editor_game_scale_parse(value, prefs.game_scale);
        } else if (key == prefs_key_workspace) {
            prefs.workspace = value;
        } else if (key == prefs_key_ui_scale) {
            if (value == ui_scale_legacy_large_str || value == ui_scale_large_str) {
                prefs.ui_scale = ui_scale_large;
            } else if (value == ui_scale_medium_str) {
                prefs.ui_scale = ui_scale_medium;
            } else {
                prefs.ui_scale = ui_scale_default;
            }
        }
    }
    return prefs;
}

std::filesystem::path rv_editor_prefs_file_path()
{
    const std::filesystem::path layout = rv_editor_layout_file_path();
    return layout.empty() ? layout : layout.parent_path() / prefs_file_name;
}

rv_editor_prefs rv_editor_prefs_load(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (path.empty() || ec || size > prefs_max_file_size) {
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
    tmp += temp_file_extension;
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
