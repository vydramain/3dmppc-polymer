#include "rv_burner_bake_models.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The program each model is baked by. Named once so the lookup and the error
// text cannot disagree about which program ran.
static constexpr const char *k_model_baker = "mppcmodbaker";

// Read back the WHOLE .mppcmod mppcmodbaker just wrote and hand it to
// rv_pdklib::rv_mppcmod_parse(). A partial check would let a truncated model
// pass here and fail only on a player's console.
static int read_mppcmod(const fs::path &path, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot reopen baked model '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot size baked model '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "baked model '" + path.string() + "' is shorter than its own reported size";
        return RV_ERR_IO;
    }

    rv_pdklib::rv_mppcmod model;
    std::string reason;
    if (rv_pdklib::rv_mppcmod_parse(bytes, model, reason) != RV_OK) {
        error = "baked model '" + path.string() + "' " + reason;
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace rv_pdktools

int rv_pdktools::bake_models(const std::string &baker_hint,
    const fs::path &disc_dir,
    const archive_plan &plan,
    std::string &error)
{
    // An empty model range needs no baker, and the loop below then does nothing.
    std::string baker;
    const int baker_err = plan.model_count == 0 ? RV_OK : find_baker(baker_hint, k_model_baker, baker, error);
    if (baker_err != RV_OK) {
        return baker_err;
    }

    for (std::size_t i = plan.first_model; i < plan.first_model + plan.model_count; ++i) {
        const archive_item &item = plan.items[i];

        // A model left by an earlier run must not pass for this run's output.
        std::error_code ec;
        fs::remove(item.payload, ec);
        if (ec) {
            error = "cannot remove the stale model '" + item.payload + "': " + ec.message();
            return RV_ERR_IO;
        }

        const std::string command =
            shell_quote(baker) + " " + shell_quote((disc_dir / item.source).string()) + " " + shell_quote(item.payload);

        std::string child_output;
        const int status = run_capture(command, child_output);
        if (status != 0) {
            dump_child_output(child_output);
            error = std::string(k_model_baker) + " failed on '" + item.source + "' (exit " + std::to_string(status) + ")";
            return RV_ERR_IO;
        }

        const int read_err = read_mppcmod(item.payload, error);
        if (read_err != RV_OK) {
            return read_err;
        }
    }

    return RV_OK;
}
