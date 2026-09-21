#include "rv_pconsole/platform/rv_pcdevchan.hpp"

namespace rv_3dmppc
{

// A player build has no development channel at all: it has no --dev field to
// set params_.dev true in the first place (rv_pboot_args.hpp), so this is
// never actually called (see rv_pconsole_run.cpp) - but it still answers
// nullptr, the channel simply not existing, not a failure to open one.
std::unique_ptr<rv_pcdevchan> rv_pcdevchan_make()
{
    return nullptr;
}

} // namespace rv_3dmppc
