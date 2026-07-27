#pragma once

#include "pdk/rv_vr.hpp"
#define RV_DISC_EXPORT(disc_class)                                                     \
    extern "C" __attribute__((visibility("default"))) rv_pdk::rv_de* mppc_disc_create( \
        int rv_pdk_major_version) {                                                    \
        if (rv_pdk_major_version != rv_pdk::RV_PDK_MAJOR_VERSION &&                    \
            rv_pdk_minor_version < rv_pdk::RV_PDK_MINOR_VERSION)                       \
            return nullptr;                                                            \
        return new disc_class();                                                       \
    }                                                                                  \
                                                                                       \
    extern "C" __attribute__((visibility("default"))) void mppc_disc_destroy(          \
        rv_pdk::rv_de* disc) {                                                         \
        delete disc;                                                                   \
    }
