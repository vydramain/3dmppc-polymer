#pragma once

// A disc that reads scenes includes this in exactly ONE of its source files. A
// disc gets pdklib as headers only (mppcburner adds no pdklib sources to it), so
// the loader and the dialect it reads with are compiled into the disc here.
// Everything else includes rv_scene.hpp alone.

#include "pdklib/rv_manifest/detail/rv_manifest_character.cpp"
#include "pdklib/rv_manifest/detail/rv_manifest_failer.cpp"
#include "pdklib/rv_manifest/detail/rv_manifest_lexer.cpp"
#include "pdklib/rv_manifest/detail/rv_manifest_parser.cpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.cpp"
#include "pdklib/rv_scene/rv_scene.cpp"
