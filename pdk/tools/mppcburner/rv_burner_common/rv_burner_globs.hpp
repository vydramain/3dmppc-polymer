#pragma once

#include "pdklib/rv_manifest/rv_manifest_glob.hpp"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

namespace rv_pdktools
{

// --- manifest globs ---
//
// The expansion (glob_expand and the directory walk behind it) lives in
// pdklib/rv_manifest/rv_manifest_glob.hpp, so every tool that expands a manifest
// uses the same walk. The burner re-exports these names under rv_pdktools.

using rv_pdklib::glob_expand;
using rv_pdklib::has_wildcard;
using rv_pdklib::split_components;
using rv_pdklib::wildcard_match;

} // namespace rv_pdktools
