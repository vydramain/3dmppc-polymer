#pragma once

#include "pdk/ve/rv_ve.hpp"

#define RV_DISC_EXPORT(disc_class)                                                             \
    extern "C" __attribute__((section(RV_MPPC_VERSION_INFO_SECTION_NAME), used, retain)) const \
    rv_pdk::rv_mppc_version_info() {                                                           \
        {'R', 'V', '_', 'M', 'P', 'P', 'C', '\0'},                                             \
        rv_pdk::RV_MPPC_VERSION_MAJOR,                                                         \
        rv_pdk::RV_MPPC_VERSION_MINOR                                                          \
    };
