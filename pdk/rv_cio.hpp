#pragma once

namespace rv_3dmppc {

// Controller Input/Output: gamepads (input) + memory card (save). DEFERRED:
// surface defined separately — see README "Status".
class rv_cio {
   public:
    virtual ~rv_cio() = default;
};

}  // namespace rv_3dmppc
