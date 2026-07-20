#pragma once

namespace rv_3dmppc {

// Controller Video (GPU). DEFERRED: surface defined separately — see README
// "Open decisions" (rv_cv granularity) and "Status".
class rv_cv {
   public:
    virtual ~rv_cv() = default;
};

}  // namespace rv_3dmppc
