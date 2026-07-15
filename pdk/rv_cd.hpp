#pragma once

namespace rv_3dmppc {

// Controller Drive — the disc DRIVE that reads the mounted .mppcdisc medium (the
// accessor is rv_pdko::drive()). DEFERRED: surface defined separately — see
// README "Status".
class rv_cd {
   public:
    virtual ~rv_cd() = default;
};

}  // namespace rv_3dmppc
