// The player build's half: no --frame-fd exists, so there is nothing to wrap.
#include "rv_pconsole/platform/rv_pcframe.hpp"

namespace rv_3dmppc
{

std::unique_ptr<rv_pcplatform> rv_pcframe_wrap(std::unique_ptr<rv_pcplatform> inner, int)
{
    return inner;
}

} // namespace rv_3dmppc
