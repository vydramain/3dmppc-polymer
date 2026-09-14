// The null platform: no window, no gamepads, no audio device. Used when a run
// wants none of the three (see rv_pcplatform_wants) or as the fallback for a
// build with no platform library at all.
#pragma once

#include <memory>

#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

std::unique_ptr<rv_pcplatform> rv_pcplatform_null_make(const rv_pcplatform_wants &wants);

} // namespace rv_3dmppc
