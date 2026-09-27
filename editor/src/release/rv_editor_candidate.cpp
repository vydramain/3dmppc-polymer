// Release candidates: their checks, the rule for approving one, their hashes.

#include "release/rv_editor_candidate.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>

#include "release/rv_editor_sha256.hpp"

namespace rv_editor
{

const char *rv_editor_check_state_name(rv_editor_check_state state)
{
    switch (state) {
        case rv_editor_check_state::not_run: return "Not run";
        case rv_editor_check_state::running: return "Running";
        case rv_editor_check_state::passed: return "Passed";
        case rv_editor_check_state::failed: return "Failed";
        case rv_editor_check_state::blocked: return "Blocked";
        case rv_editor_check_state::skipped: return "Skipped";
    }
    return "?";
}

std::vector<rv_editor_check> rv_editor_checks_make()
{
    // Each claims only what its name says; together they are not a proof that
    // the whole game works.
    constexpr struct
    {
        const char *name;
        const char *passes_when;
        bool manual;
    } list[] = {
        { "Build", "mppcburner exited 0 and wrote this image", false },
        { "Disc loads", "the console mounted this image and answered its first status", false },
        { "Launch", "the game reaches its first screen", true },
        { "Input", "the controls do what the game says they do", true },
        { "Audio", "music and effects play, without gaps or noise", true },
        { "Main scenario", "the game's main path plays through as the project defines it", true },
        { "Exit", "the game leaves by its own way out, or by Stop (quit), without a crash", true },
    };
    std::vector<rv_editor_check> checks;
    for (const auto &c : list) {
        rv_editor_check check{};
        check.name = c.name;
        check.passes_when = c.passes_when;
        check.manual = c.manual;
        checks.push_back(check);
    }
    return checks;
}

bool rv_editor_check_valid(const rv_editor_candidate &c, const rv_editor_check &check)
{
    return !c.bytes_changed && !c.sha256.empty() && check.hash == c.sha256;
}

void rv_editor_check_set(rv_editor_candidate &c, size_t id, rv_editor_check_state state, const std::string &note,
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
        return "The image is not hashed yet";
    }
    if (c.bytes_changed) {
        return "The image's bytes changed after it was built: its results do not apply";
    }
    if (c.hashing.valid()) {
        return "Verifying the image's bytes";
    }
    if (c.verified_hash != c.sha256) {
        return "Verify the image's bytes first";
    }
    for (const rv_editor_check &check : c.checks) {
        if (check.state != rv_editor_check_state::passed || !rv_editor_check_valid(c, check)) {
            return "Every required check must pass on these bytes; Skipped does not count";
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
    return std::to_string(passed) + " of " + std::to_string(c.checks.size()) + " required checks passed";
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
    const char *user = std::getenv("USER");
    return user != nullptr && user[0] != '\0' ? user : "unknown operator";
}

std::string rv_editor_wall_clock()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

} // namespace rv_editor
