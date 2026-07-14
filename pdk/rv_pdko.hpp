#pragma once

#include "rv_ca.hpp"
#include "rv_cd.hpp"
#include "rv_cio.hpp"
#include "rv_cv.hpp"

namespace rv_3dmppc {

class rv_pdko {
   public:
    rv_ca get_controller_audio();
    rv_cd get_controller_disk();
    rv_cio get_controller_input_output();
    rv_cv get_controller_video();
};

};  // namespace rv_3dmppc
