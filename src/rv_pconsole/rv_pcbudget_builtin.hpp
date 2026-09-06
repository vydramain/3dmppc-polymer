// The BUILT-IN disc's requirements, as data.
//
// A disc declares what it needs in its manifest [budget], and that declaration
// is what it gets — the numbers become the machine it runs on. The built-in
// service test carries no manifest, so its requirements live here instead:
// the reference console's own answers, copied verbatim from
// docs/platform/specs.md, "Target Spec".
//
// This is NOT a ceiling on what other discs may ask for. What a disc may ask
// for is bounded by the machine it is asked to run on, and by nothing else.
#pragma once

#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_3dmppc
{

inline const rv_pdklib::rv_manifest_budget &rv_pcbudget_builtin()
{
    static const rv_pdklib::rv_manifest_budget kBudget = {
        .pcca = {
            .voice_count = 24,
            .sound_memory_size = 512 * 1024,
        },
        .pccv = {
            .screen_width = 320,
            .screen_height = 240,
            .texture_max_width = 256,
            .texture_max_height = 256,
            .video_memory_size = 1 * 1024 * 1024,
            .frame_capacity = 4096,
            .ot_bucket_count = 1024,
        },
        .pccio = {
            .iport_count = 2,
        },
        .pccm = {
            .card_slots = 16,
            .card_slot_size = 8 * 1024,
        },
        .pccd = {
            .code_entry = std::string(),
        },
    };
    return kBudget;
}

} // namespace rv_3dmppc
