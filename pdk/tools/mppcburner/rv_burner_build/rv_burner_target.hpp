#pragma once

#include <string>
#include <string_view>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_build/rv_burner_build_assets.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// One -t value. resolve picks the destination before the compile, so a refusal
// stops early; a player directory it creates stays if the build fails later, as
// the burn writes into it. finish adds what goes beside the image after the burn,
// and is null when the image is the whole product.
using rv_burner_target_resolve = int (*)(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &manifest,
    rv_burner_destination &destination,
    std::string &error);
using rv_burner_target_finish = int (*)(const rv_burner_destination &destination, std::string &error);

struct rv_burner_target_spec {
    const char *name;
    rv_burner_target_resolve resolve;
    rv_burner_target_finish finish;
};

/// The target a -t name selects. An empty name is the default, disc.
///
/// @return the row, or nullptr for a name no row has
const rv_burner_target_spec *find_target(std::string_view name);

/// The names find_target() accepts, comma separated, for a refusal message.
std::string target_names();

} // namespace rv_pdktools
