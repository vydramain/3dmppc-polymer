#include "rv_pconsole/platform/rv_pcdevchan.hpp"

namespace rv_3dmppc
{

// A player build has no development channel at all: --dev is refused before
// this is ever called (see rv_pconsole_run.cpp), so nullptr here is the
// channel simply not existing, not a failure to open one.
std::unique_ptr<rv_pcdevchan> rv_pcdevchan_make()
{
    return nullptr;
}

} // namespace rv_3dmppc
