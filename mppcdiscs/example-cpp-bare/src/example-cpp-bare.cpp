#include "pdk/de/rv_dv.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"

namespace example_cpp_bare
{

namespace
{

// The frame's clear colour: the same dark blue as example-cpp.
constexpr rv_color RV_EXAMPLE_CPP_BARE_BACKGROUND = { 20, 24, 40 };

} // namespace

// The base supplies disc_initialize, frame_update and disc_release.
RV_MPPC_DISC_CPP_DEF(rv_dmain_base_)

class rv_dmain : public rv_dmain_base_
{
public:
    void frame_render()
    {
        frame_begin(RV_EXAMPLE_CPP_BARE_BACKGROUND);
        frame_end();
    }

    void disc_shutdown()
    {
        // Nothing to release: the drive frees the disc's assets when it unloads.
    }

    const char *disc_title() const
    {
        return "example-cpp-bare";
    }
};

} // namespace example_cpp_bare

RV_MPPC_DISC_ENTRY_DEF(example_cpp_bare::rv_dmain);
