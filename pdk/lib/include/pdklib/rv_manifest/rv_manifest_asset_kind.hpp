#pragma once

#include <string_view>

namespace rv_pdklib
{

// --- disc asset kind by extension ---
//
// The one table of which kind a file in disc.toml's [assets] is. The burner (packing)
// and the editor (opening) are to read it; neither includes it yet. Pure lookup, no I/O.

enum class rv_manifest_asset_kind {
    texture,
    sound,
    model,
    // No extension maps to stream: the [assets.stream] section does.
    stream,
    copy,
};

// Extensions match exactly, case included: the editor tests with ends_with and the
// burner's glob matching compares characters exactly, so ".PNG" is a copy.
inline constexpr std::string_view rv_manifest_asset_ext_png = ".png";
inline constexpr std::string_view rv_manifest_asset_ext_wav = ".wav";
inline constexpr std::string_view rv_manifest_asset_ext_gltf = ".gltf";
inline constexpr std::string_view rv_manifest_asset_ext_glb = ".glb";
inline constexpr std::string_view rv_manifest_asset_ext_mp3 = ".mp3";

struct rv_manifest_asset_ext {
    std::string_view extension;
    rv_manifest_asset_kind kind;
};

// No entry is a suffix of another, so the first match is the only match.
inline constexpr rv_manifest_asset_ext rv_manifest_asset_exts[] = {
    { rv_manifest_asset_ext_png, rv_manifest_asset_kind::texture },
    { rv_manifest_asset_ext_wav, rv_manifest_asset_kind::sound },
    { rv_manifest_asset_ext_gltf, rv_manifest_asset_kind::model },
    { rv_manifest_asset_ext_glb, rv_manifest_asset_kind::model },
    { rv_manifest_asset_ext_mp3, rv_manifest_asset_kind::sound },
};

/// Kind of the file with this name or relative path.
///
/// @param name  a file name or a path relative to the disc directory
/// @return the table's kind for the extension; copy when no entry matches
inline rv_manifest_asset_kind rv_manifest_asset_kind_of(std::string_view name)
{
    for (const rv_manifest_asset_ext &entry : rv_manifest_asset_exts) {
        if (name.ends_with(entry.extension)) {
            return entry.kind;
        }
    }
    return rv_manifest_asset_kind::copy;
}

} // namespace rv_pdklib
