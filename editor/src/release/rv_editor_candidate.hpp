#pragma once

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace rv_editor
{

// A release candidate: one disc image mppcburner wrote, under a number no other
// image in the directory holds, identified by the SHA-256 of its bytes. What was checked on it
// belongs to that hash and is kept on disk beside it (release/rv_editor_candidate_store.hpp).

enum class rv_editor_check_state
{
    not_run,
    running,
    passed,
    failed,
    blocked,
    skipped,
};

const char *rv_editor_check_state_name(rv_editor_check_state state);

struct rv_editor_check
{
    const char *name;
    const char *passes_when; // what a Passed claims
    bool manual;             // the operator decides; the editor never passes it
    rv_editor_check_state state = rv_editor_check_state::not_run;
    std::string note;        // what was seen: why it failed, what ran
    std::string env;         // the runtime it was checked on
    std::string at;          // when the result was set
    std::string hash;        // the image's SHA-256 the result is for
};

// The checks a candidate needs, all required; Skipped does not count as Passed.
// Order: the two the editor runs, then the operator's.
enum rv_editor_check_id : size_t
{
    rv_editor_check_build,
    rv_editor_check_loads,
    rv_editor_check_launch,
    rv_editor_check_input,
    rv_editor_check_audio,
    rv_editor_check_scenario,
    rv_editor_check_exit,
    rv_editor_check_count,
};

enum class rv_editor_decision
{
    none,
    approved,
    rejected,
};

struct rv_editor_hash_result
{
    std::string hex;
    uint64_t size = 0;
    std::string error;
};

struct rv_editor_candidate
{
    uint32_t number = 0;
    std::filesystem::path image;
    std::string command;     // the burner's command line
    std::string built_at;
    std::string burner;      // `--version` lines of the tools that made it
    std::string baker;
    std::string sha256;      // of the bytes as built; empty until hashed
    uint64_t size = 0;
    std::string hash_error;
    // The last verification: the bytes read again. A different hash makes every
    // result above void for good (R-03): the file is no longer what was checked.
    std::string verified_hash;
    std::string verified_at;
    bool bytes_changed = false;
    std::future<rv_editor_hash_result> hashing;
    bool tree_changed = false; // a project file changed after the build started
    std::vector<rv_editor_check> checks;
    rv_editor_decision decision = rv_editor_decision::none;
    std::string decided_at;
    // Approve asked: the bytes are read once more and the decision taken only
    // if they are still the ones checked.
    bool approve_pending = false;
    uint32_t playtests = 0;    // how many ran; each one's log is kept beside the record
    std::string last_run_end;  // how the last playtest ended
    bool last_run_clean = false; // ended by quit or by itself, not forced, crashed or refused
    std::string source_revision; // the sources' commit and whether they differed from it (REL-01)
    std::string operator_name;   // who approved or rejected it (REL-05)
    bool dirty = false;          // changed since its record was written
};

// The candidate's checks, all Not run.
std::vector<rv_editor_check> rv_editor_checks_make();

// A result counts only for the bytes it was set on.
bool rv_editor_check_valid(const rv_editor_candidate &c, const rv_editor_check &check);

// Sets a check's result for the candidate's current hash; a changed result
// takes back any decision made on the old ones.
void rv_editor_check_set(rv_editor_candidate &c, size_t id, rv_editor_check_state state, const std::string &note,
    const std::string &env);

// Why the candidate cannot be approved now; nullptr when it can: every check
// Passed on these bytes and the bytes verified unchanged.
const char *rv_editor_why_not_approve(const rv_editor_candidate &c);

// Passed checks and open ones, as "3 of 7 passed".
std::string rv_editor_checks_summary(const rv_editor_candidate &c);

// Reads the image again in the background; rv_editor_candidate_poll takes the answer.
void rv_editor_candidate_hash(rv_editor_candidate &c);
// Once a frame: a finished hash becomes the candidate's hash (the first) or its verification.
void rv_editor_candidate_poll(rv_editor_candidate &c);

// "2026-09-26 21:04:05".
std::string rv_editor_wall_clock();

// Who decides: the login name the session gives ($USER), else "unknown operator".
std::string rv_editor_operator();

} // namespace rv_editor
