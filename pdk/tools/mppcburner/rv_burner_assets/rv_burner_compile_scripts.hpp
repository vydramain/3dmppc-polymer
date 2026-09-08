#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"

namespace rv_pdktools
{

// --- lua to bytecode ---
//
// A disc ships compiled bytecode, not source. The console's VM then loads a
// chunk instead of parsing one, so a syntax error is the burner's refusal on a
// developer's terminal rather than a runtime failure in front of a player.
//
// The compiler is the LuaJIT the burner is LINKED against, not a `luajit -b`
// found on $PATH. Bytecode is version-specific, so tying it to the linked
// library ties it to a version this repository pins, instead of to whatever
// happens to be installed on the machine that burned the disc.

/// Compile every planned script to a .luac file at its planned payload path.
///
/// Called after the plan's collision check, so no two scripts can be written to
/// one bytecode file.
///
/// @param plan      the planned archive; only its script range is touched
/// @param disc_dir  absolute disc directory the sources are relative to
/// @param error     set naming the script and what luajit said about it
/// @return 0 on success, 1 on refusal
int compile_scripts(
    const archive_plan &plan,
    const std::filesystem::path &disc_dir,
    std::string &error);

/// Compile one .lua file into one .luac file.
///
/// @param lua_path  the source to read
/// @param out_path  the bytecode file to create, truncating any existing one
/// @param error     set with luajit's own message, or with the I/O failure
/// @return 0 on success, 1 on refusal
int compile_simple_script(
    const std::string &lua_path,
    const std::string &out_path,
    std::string &error);

} // namespace rv_pdktools
