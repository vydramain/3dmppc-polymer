#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "font/rv_editor_font.hpp"

namespace rv_editor
{

// How the Game tile scales the console's frame: fit fills the tile keeping the
// frame's proportions, integer takes the largest whole multiple that fits, the
// rest are fixed multiples.
enum class rv_editor_game_scale
{
    fit,
    integer,
    x1,
    x2,
    x3,
};

// "fit", "integer", "1x", "2x", "3x", as the view file and the menu name them.
const char *rv_editor_game_scale_name(rv_editor_game_scale scale);
bool rv_editor_game_scale_parse(std::string_view name, rv_editor_game_scale &scale);

// What the user chose for the view, kept between runs in its own file next to
// the layout, never in settings.toml or disc.toml.
struct rv_editor_prefs
{
    rv_editor_code_size code_size = rv_editor_code_size::normal;
    rv_editor_game_scale game_scale = rv_editor_game_scale::fit;
};

// "3dmppc-editor-view 1", then one "key value" line per setting.
std::string rv_editor_prefs_write(const rv_editor_prefs &prefs);
// The defaults for a text that is not a view file; within one, an unknown key or
// value keeps its default and the rest still count.
rv_editor_prefs rv_editor_prefs_read(std::string_view text);

// "view" next to the layout file; empty when the layout path is.
std::filesystem::path rv_editor_prefs_file_path();
// The defaults when the file is missing or unreadable.
rv_editor_prefs rv_editor_prefs_load(const std::filesystem::path &path);
// Written to "<path>.tmp" and renamed over `path`. False with the reason.
bool rv_editor_prefs_save(const std::filesystem::path &path, const rv_editor_prefs &prefs, std::string &error);

} // namespace rv_editor
