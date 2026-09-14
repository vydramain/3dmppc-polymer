// The SDL3 platform. SDL-free on purpose: everything above this header speaks
// rv_pcplatform, never SDL types. The dependency on <SDL3/SDL.h> stops at
// rv_pcplatform_sdl3_detail.hpp and the .cpp files under this directory.
#pragma once

#include <memory>

#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

std::unique_ptr<rv_pcplatform> rv_pcplatform_sdl3_make(const rv_pcplatform_wants &wants);

} // namespace rv_3dmppc
