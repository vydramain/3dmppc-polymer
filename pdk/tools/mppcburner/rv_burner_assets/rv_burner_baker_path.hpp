#pragma once

#include <string>

namespace rv_pdktools
{

// --- finding the bakers ---
//
// A baker (mppctexbaker for textures, mppcmusbaker for sounds, more later) is a
// separate PROGRAM the burner runs, not a library it links. Linking it would
// pull stb_image — an image decoder — into the burner, and the tools are
// shipped, versioned and rebuilt independently; a subprocess keeps that
// boundary and lets `--baker` point at another build entirely. The cost is
// this file: a program has to be found before it can be run, whereas a
// function is simply called.

/// Locate the executable named @p program.
///
/// @p hint is a directory to search, or a file whose real directory is searched
/// (symlinks resolved), so `--baker .../mppctexbaker` works as well as a directory.
/// Without a hint the search goes next to the running binary (the usual case:
/// all tools sit in one build tree), then $PATH.
///
/// @param hint     value of `--baker`; empty means search
/// @param program  file name to find, e.g. "mppctexbaker"
/// @param out      receives the path of an executable file
/// @param error    set on every failure, naming where the search looked
/// @return RV_OK when @p out holds a runnable program; RV_ERR_NOENT if the program
/// is not found; RV_ERR_INVAL if the hint cannot be used, including a program that
/// is not executable
int find_baker(const std::string &hint, const std::string &program, std::string &out, std::string &error);

} // namespace rv_pdktools
