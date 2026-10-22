#pragma once

#include "rv_manifest_tree.hpp"
#include "../rv_manifest.hpp"

namespace rv_pdklib
{

// No rv_texfmt enumerator is 0 and no rv_texfmt_names row has it. An unknown
// [assets.texture] format binds to this, so it cannot silently become the idx8
// default; rv_manifest_validate refuses it.
inline constexpr rv_texfmt rv_manifest_texfmt_unknown_spelling = static_cast<rv_texfmt>(0);

// Stage 4: a checked tree → rv_manifest. The ONLY place that knows the field
// names of the structure; every stage before it speaks in sections and keys.
// Run it on a tree semantic analysis approved — it assumes the kinds match and
// silently ignores anything the schema does not name.
rv_manifest rv_manifest_bind(const rv_manifest_tree &tree);

} // namespace rv_pdklib
