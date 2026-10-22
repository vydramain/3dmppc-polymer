#pragma once

// A disc that reads scenes includes this in exactly ONE of its source files. A
// disc gets pdklib as headers only (mppcburner adds no pdklib sources to it), so
// the scene writer (rv_scene_render.cpp) is compiled into the disc here until it becomes
// a header too; the loader and the manifest dialect are inline. Everything else
// includes rv_scene.hpp alone.

#include "pdklib/rv_scene/rv_scene_render.cpp"
