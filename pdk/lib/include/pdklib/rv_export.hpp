#pragma once

#include "pdk/ve/rv_ve.hpp"

#define RV_DISC_EXPORT(disc_class)                                                     \
    extern "C" __attribute__((visibility("default"))) rv_pdk::rv_de* mppc_disc_create( \
        int version_major, int version_minor) {                                        \
        if (version_major != rv_pdk::RV_3DMPPC_VERSION_MAJOR ||                        \
            version_minor > rv_pdk::RV_3DMPPC_VERSION_MINOR)                           \
            return nullptr;                                                            \
        return new disc_class();                                                       \
    }                                                                                  \
    extern "C" __attribute__((visibility("default"))) void mppc_disc_destroy(          \
        rv_pdk::rv_de* disc) {                                                         \
        delete disc;                                                                   \
    }
