#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "release/rv_editor_candidate.hpp"

namespace rv_editor
{

// A candidate's record, <dir>/<n>.toml: identity, hash, checks and decision,
// written whole. RV_OK or RV_ERR_IO with the reason in error.
int rv_editor_candidate_save(const std::filesystem::path &dir, const rv_editor_candidate &c, std::string &error);

// The records in `dir` by number. One that does not read is left out and named in
// `errors`; a check a closed window left Running comes back Not run, never Passed.
std::vector<rv_editor_candidate> rv_editor_candidates_load(const std::filesystem::path &dir,
    std::vector<std::string> &errors);

// A log kept beside the record: "build", "playtest-1", ... as <dir>/<n>-<what>.log.
std::filesystem::path rv_editor_candidate_log(const std::filesystem::path &dir, uint32_t number, const std::string &what);

} // namespace rv_editor
