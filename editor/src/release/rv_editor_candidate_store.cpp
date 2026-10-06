// Candidate records on disk: what the checks and the decision said outlives the
// window, and a record half written is never read as a result.

#include "release/rv_editor_candidate_store.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

// Prefix of check section names in TOML.
constexpr std::string_view check_prefix = "check.";

// Candidate record section name and keys.
constexpr std::string_view candidate_section = "candidate";
constexpr std::string_view key_number = "number";
constexpr std::string_view key_image = "image";
constexpr std::string_view key_command = "command";
constexpr std::string_view key_built_at = "built_at";
constexpr std::string_view key_burner = "burner";
constexpr std::string_view key_baker = "baker";
constexpr std::string_view key_source_revision = "source_revision";
constexpr std::string_view key_sha256 = "sha256";
constexpr std::string_view key_size = "size";
constexpr std::string_view key_verified_hash = "verified_hash";
constexpr std::string_view key_verified_at = "verified_at";
constexpr std::string_view key_bytes_changed = "bytes_changed";
constexpr std::string_view key_tree_changed = "tree_changed";
constexpr std::string_view key_decision = "decision";
constexpr std::string_view key_decided_at = "decided_at";
constexpr std::string_view key_operator = "operator";
constexpr std::string_view key_playtests = "playtests";
constexpr std::string_view key_last_run_end = "last_run_end";
constexpr std::string_view key_last_run_clean = "last_run_clean";

// Check result keys within check sections.
constexpr std::string_view key_state = "state";
constexpr std::string_view key_note = "note";
constexpr std::string_view key_env = "env";
constexpr std::string_view key_at = "at";
constexpr std::string_view key_hash = "hash";

constexpr std::array<const char *, rv_editor_check_count> rv_editor_check_keys = { "build", "loads", "player",
    "launch", "input", "audio", "scenario", "exit" };
constexpr std::array<const char *, 6> rv_editor_state_keys = { "not_run", "running", "passed", "failed", "blocked",
    "skipped" };
constexpr std::array<const char *, 3> rv_editor_decision_keys = { "none", "approved", "rejected" };

template <size_t N> size_t rv_editor_key_index(const std::array<const char *, N> &keys, std::string_view key)
{
    for (size_t i = 0; i < N; ++i) {
        if (key == keys[i]) {
            return i;
        }
    }
    return N;
}

void rv_editor_candidate_field(rv_editor_candidate &c, const rv_pdklib::rv_manifest_tree_entry &e)
{
    const std::string &s = e.value.str;
    const int64_t n = e.value.num;
    if (e.value.kind == rv_pdklib::rv_manifest_value_kind::integer) {
        if (e.key == key_number) {
            c.number = static_cast<uint32_t>(n);
        } else if (e.key == key_size) {
            c.size = static_cast<uint64_t>(n);
        } else if (e.key == key_bytes_changed) {
            c.bytes_changed = n != 0;
        } else if (e.key == key_tree_changed) {
            c.tree_changed = n != 0;
        } else if (e.key == key_last_run_clean) {
            c.last_run_clean = n != 0;
        } else if (e.key == key_playtests) {
            c.playtests = static_cast<uint32_t>(n);
        }
        return;
    }
    std::string *field = e.key == key_command ? &c.command : e.key == key_built_at ? &c.built_at :
        e.key == key_burner                                                        ? &c.burner :
        e.key == key_baker                                                         ? &c.baker :
        e.key == key_sha256                                                        ? &c.sha256 :
        e.key == key_verified_hash                                                 ? &c.verified_hash :
        e.key == key_verified_at                                                   ? &c.verified_at :
        e.key == key_source_revision                                               ? &c.source_revision :
        e.key == key_decided_at                                                    ? &c.decided_at :
        e.key == key_operator                                                      ? &c.operator_name :
        e.key == key_last_run_end                                                  ? &c.last_run_end :
                                                                                     nullptr;
    if (field != nullptr) {
        *field = s;
    } else if (e.key == key_image) {
        c.image = s;
    } else if (e.key == key_decision) {
        const size_t d = rv_editor_key_index(rv_editor_decision_keys, s);
        c.decision = d < rv_editor_decision_keys.size() ? static_cast<rv_editor_decision>(d) : rv_editor_decision::none;
    }
}

void rv_editor_check_field(rv_editor_check &check, const rv_pdklib::rv_manifest_tree_entry &e)
{
    const std::string &s = e.value.str;
    if (e.key == key_state) {
        const size_t st = rv_editor_key_index(rv_editor_state_keys, s);
        check.state = st < rv_editor_state_keys.size() ? static_cast<rv_editor_check_state>(st)
                                                        : rv_editor_check_state::not_run;
    } else if (e.key == key_note) {
        check.note = s;
    } else if (e.key == key_env) {
        check.env = s;
    } else if (e.key == key_at) {
        check.at = s;
    } else if (e.key == key_hash) {
        check.hash = s;
    }
}

} // namespace

std::filesystem::path rv_editor_candidate_log(const std::filesystem::path &dir, uint32_t number, const std::string &what)
{
    return dir / (std::to_string(number) + "-" + what + ".log");
}

int rv_editor_candidate_save(const std::filesystem::path &dir, const rv_editor_candidate &c, std::string &error)
{
    const auto q = [](std::string_view s) { return rv_editor_toml_quote(s); };
    std::string t = "# A release candidate of 3dmppc-editor; the editor rewrites this file.\n\n[candidate]\n";
    t += std::string(key_number) + " = " + std::to_string(c.number) + "\n";
    t += std::string(key_image) + " = " + q(c.image.string()) + "\n";
    t += std::string(key_command) + " = " + q(c.command) + "\n";
    t += std::string(key_built_at) + " = " + q(c.built_at) + "\n";
    t += std::string(key_burner) + " = " + q(c.burner) + "\n";
    t += std::string(key_baker) + " = " + q(c.baker) + "\n";
    t += std::string(key_source_revision) + " = " + q(c.source_revision) + "\n";
    t += std::string(key_sha256) + " = " + q(c.sha256) + "\n";
    t += std::string(key_size) + " = " + std::to_string(c.size) + "\n";
    t += std::string(key_verified_hash) + " = " + q(c.verified_hash) + "\n";
    t += std::string(key_verified_at) + " = " + q(c.verified_at) + "\n";
    t += std::string(key_bytes_changed) + " = " + std::to_string(c.bytes_changed ? 1 : 0) + "\n";
    t += std::string(key_tree_changed) + " = " + std::to_string(c.tree_changed ? 1 : 0) + "\n";
    t += std::string(key_decision) + " = " + q(rv_editor_decision_keys[static_cast<size_t>(c.decision)]) + "\n";
    t += std::string(key_decided_at) + " = " + q(c.decided_at) + "\n";
    t += std::string(key_operator) + " = " + q(c.operator_name) + "\n";
    t += std::string(key_playtests) + " = " + std::to_string(c.playtests) + "\n";
    t += std::string(key_last_run_end) + " = " + q(c.last_run_end) + "\n";
    t += std::string(key_last_run_clean) + " = " + std::to_string(c.last_run_clean ? 1 : 0) + "\n";
    for (size_t i = 0; i < c.checks.size() && i < rv_editor_check_keys.size(); ++i) {
        const rv_editor_check &check = c.checks[i];
        t += std::string("\n[check.") + rv_editor_check_keys[i] + "]\n";
        t += std::string(key_state) + " = " + q(rv_editor_state_keys[static_cast<size_t>(check.state)]) + "\n";
        t += std::string(key_note) + " = " + q(check.note) + "\n";
        t += std::string(key_env) + " = " + q(check.env) + "\n";
        t += std::string(key_at) + " = " + q(check.at) + "\n";
        t += std::string(key_hash) + " = " + q(check.hash) + "\n";
    }
    const int code = rv_editor_file_replace(dir / (std::to_string(c.number) + ".toml"), t, error);
    if (code != RV_OK) {
        return code;
    }
    return RV_OK;
}

std::vector<rv_editor_candidate> rv_editor_candidates_load(const std::filesystem::path &dir,
    std::vector<std::string> &errors)
{
    std::vector<std::filesystem::path> records;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string stem = it->path().stem().string();
        uint32_t n = 0;
        const auto [ptr, err] = std::from_chars(stem.data(), stem.data() + stem.size(), n);
        if (it->path().extension() == ".toml" && err == std::errc{} && ptr == stem.data() + stem.size()) {
            records.push_back(it->path());
        }
    }
    std::vector<rv_editor_candidate> out;
    for (const std::filesystem::path &path : records) {
        rv_pdklib::rv_manifest_tree tree;
        std::string error;
        if (rv_pdklib::rv_manifest_read_tree(rv_editor_file_text(path), path.string(), tree, error) != 0) {
            errors.push_back(error);
            continue;
        }
        rv_editor_candidate c;
        c.checks = rv_editor_checks_make();
        for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
            if (section.name == candidate_section) {
                for (const auto &e : section.entries) {
                    rv_editor_candidate_field(c, e);
                }
                continue;
            }
            size_t id = rv_editor_check_keys.size();
            if (section.name.starts_with(check_prefix)) {
                const std::string_view key = std::string_view(section.name).substr(check_prefix.size());
                id = rv_editor_key_index(rv_editor_check_keys, key);
            }
            if (id < c.checks.size()) {
                for (const auto &e : section.entries) {
                    rv_editor_check_field(c.checks[id], e);
                }
            }
        }
        if (c.number == 0 || c.image.empty()) {
            errors.push_back(path.string() + ": not a candidate record");
            continue;
        }
        for (rv_editor_check &check : c.checks) {
            if (check.state == rv_editor_check_state::running) {
                check.state = rv_editor_check_state::not_run;
                check.note = "interrupted: the editor closed while it ran";
                c.dirty = true;
            }
        }
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const rv_editor_candidate &a, const rv_editor_candidate &b) {
        return a.number < b.number;
    });
    return out;
}

} // namespace rv_editor
