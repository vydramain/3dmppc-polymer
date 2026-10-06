// Release candidates: their checks, the rule for approving one, their hashes.

#include "release/rv_editor_candidate.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>

#include "release/rv_editor_sha256.hpp"

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Time format buffer; sized for "%Y-%m-%d %H:%M:%S" plus null terminator (19 bytes minimum).
constexpr size_t time_format_buffer_bytes = 32;

// Environment variable for the current user login.
constexpr const char *user_env_var_name = "USER";

// Default operator name when the USER environment variable is unset.
constexpr const char *default_operator_name = "unknown operator";

} // namespace

const char *rv_editor_check_state_name(rv_editor_check_state state)
{
    switch (state) {
    case rv_editor_check_state::not_run:
        return rv_editor_text("candidate.check_state_not_run");
    case rv_editor_check_state::running:
        return rv_editor_text("candidate.check_state_running");
    case rv_editor_check_state::passed:
        return rv_editor_text("candidate.check_state_passed");
    case rv_editor_check_state::failed:
        return rv_editor_text("candidate.check_state_failed");
    case rv_editor_check_state::blocked:
        return rv_editor_text("candidate.check_state_blocked");
    case rv_editor_check_state::skipped:
        return rv_editor_text("candidate.check_state_skipped");
    }
    return "?";
}

std::vector<rv_editor_check> rv_editor_checks_make()
{
    // Each claims only what its name says; together they are not a proof that
    // the whole game works.
    constexpr struct {
        const char *name_key;
        const char *passes_when_key;
        bool manual;
    } list[] = {
        { "candidate.build_name", "candidate.build_passes_when", false },
        { "candidate.disc_loads_name", "candidate.disc_loads_passes_when", false },
        { "candidate.player_name", "candidate.player_passes_when", false },
        { "candidate.launch_name", "candidate.launch_passes_when", true },
        { "candidate.input_name", "candidate.input_passes_when", true },
        { "candidate.audio_name", "candidate.audio_passes_when", true },
        { "candidate.scenario_name", "candidate.scenario_passes_when", true },
        { "candidate.exit_name", "candidate.exit_passes_when", true },
    };
    std::vector<rv_editor_check> checks;
    for (const auto &c : list) {
        rv_editor_check check{};
        check.name = rv_editor_text(c.name_key);
        check.passes_when = rv_editor_text(c.passes_when_key);
        check.manual = c.manual;
        checks.push_back(check);
    }
    return checks;
}

bool rv_editor_check_valid(const rv_editor_candidate &c, const rv_editor_check &check)
{
    return !c.bytes_changed && !c.sha256.empty() && check.hash == c.sha256;
}

void rv_editor_check_set(rv_editor_candidate &c,
    size_t id,
    rv_editor_check_state state,
    const std::string &note,
    const std::string &env)
{
    rv_editor_check &check = c.checks[id];
    check.state = state;
    check.note = note;
    check.env = env;
    check.at = rv_editor_wall_clock();
    check.hash = c.sha256;
    c.dirty = true;
    if (c.decision != rv_editor_decision::rejected) {
        c.decision = rv_editor_decision::none;
        c.decided_at.clear();
    }
}

const char *rv_editor_why_not_approve(const rv_editor_candidate &c)
{
    if (c.sha256.empty()) {
        return rv_editor_text("candidate.why_not_approve_no_hash");
    }
    if (c.bytes_changed) {
        return rv_editor_text("candidate.why_not_approve_bytes_changed");
    }
    if (c.hashing.valid()) {
        return rv_editor_text("candidate.why_not_approve_hashing");
    }
    if (c.verified_hash != c.sha256) {
        return rv_editor_text("candidate.why_not_approve_verify_first");
    }
    for (const rv_editor_check &check : c.checks) {
        if (check.state != rv_editor_check_state::passed || !rv_editor_check_valid(c, check)) {
            return rv_editor_text("candidate.why_not_approve_failed_checks");
        }
    }
    return nullptr;
}

std::string rv_editor_checks_summary(const rv_editor_candidate &c)
{
    size_t passed = 0;
    for (const rv_editor_check &check : c.checks) {
        passed += check.state == rv_editor_check_state::passed && rv_editor_check_valid(c, check) ? 1 : 0;
    }
    const std::string passed_str = std::to_string(passed);
    const std::string total_str = std::to_string(c.checks.size());
    return rv_editor_text_format("candidate.checks_summary_format", std::make_format_args(passed_str, total_str));
}

void rv_editor_candidate_hash(rv_editor_candidate &c)
{
    if (c.hashing.valid()) {
        return;
    }
    c.hashing = std::async(std::launch::async, [image = c.image] {
        rv_editor_hash_result r;
        r.hex = rv_editor_sha256_file(image, r.size, r.error);
        return r;
    });
}

void rv_editor_candidate_poll(rv_editor_candidate &c)
{
    if (!c.hashing.valid() || c.hashing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const rv_editor_hash_result r = c.hashing.get();
    c.dirty = true;
    if (c.sha256.empty()) {
        c.sha256 = r.hex;
        c.size = r.size;
        c.hash_error = r.error;
        c.verified_hash = r.hex;
        c.verified_at = rv_editor_wall_clock();
        if (!r.hex.empty()) {
            // Built and hashed: the build check holds for exactly these bytes.
            rv_editor_check_set(c, rv_editor_check_build, rv_editor_check_state::passed, c.command, c.burner);
        }
        return;
    }
    c.verified_hash = r.hex;
    c.verified_at = rv_editor_wall_clock();
    c.hash_error = r.error;
    // Gone or different: either way the results no longer describe a file that exists.
    c.bytes_changed = c.bytes_changed || r.hex != c.sha256;
    if (c.approve_pending && rv_editor_why_not_approve(c) == nullptr) {
        c.decision = rv_editor_decision::approved;
        c.decided_at = c.verified_at;
        c.operator_name = rv_editor_operator();
    }
    c.approve_pending = false;
}

std::string rv_editor_operator()
{
    const char *user = std::getenv(user_env_var_name);
    return user != nullptr && user[0] != '\0' ? user : default_operator_name;
}

std::string rv_editor_wall_clock()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[time_format_buffer_bytes];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

} // namespace rv_editor
