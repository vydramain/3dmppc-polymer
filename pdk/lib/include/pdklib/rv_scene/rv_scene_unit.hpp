#pragma once

// A disc that reads scenes includes this in exactly ONE of its source files. A
// disc gets pdklib as headers only (mppcburner adds no pdklib sources to it), so
// the scene loader is compiled into the disc here; the manifest dialect is inline.
// Everything else includes rv_scene.hpp alone.

#include "pdklib/rv_scene/rv_scene.cpp"
