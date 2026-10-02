// Candidate records on disk: what the checks and the decision said outlives the
// window, and a record half written is never read as a result.

#include "release/rv_editor_candidate_store.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"

namespace rv_editor
{

namespace
{

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
        if (e.key == "number") {
            c.number = static_cast<uint32_t>(n);
        } else if (e.key == "size") {
            c.size = static_cast<uint64_t>(n);
        } else if (e.key == "bytes_changed") {
            c.bytes_changed = n != 0;
        } else if (e.key == "tree_changed") {
            c.tree_changed = n != 0;
        } else if (e.key == "last_run_clean") {
            c.last_run_clean = n != 0;
        } else if (e.key == "playtests") {
            c.playtests = static_cast<uint32_t>(n);
        }
        return;
    }
    std::string *field = e.key == "command" ? &c.command : e.key == "built_at" ? &c.built_at
        : e.key == "burner"          ? &c.burner
        : e.key == "baker"           ? &c.baker
        : e.key == "sha256"          ? &c.sha256
        : e.key == "verified_hash"   ? &c.verified_hash
        : e.key == "verified_at"     ? &c.verified_at
        : e.key == "source_revision" ? &c.source_revision
        : e.key == "decided_at"      ? &c.decided_at
        : e.key == "operator"        ? &c.operator_name
        : e.key == "last_run_end"    ? &c.last_run_end
                                     : nullptr;
    if (field != nullptr) {
        *field = s;
    } else if (e.key == "image") {
        c.image = s;
    } else if (e.key == "decision") {
        const size_t d = rv_editor_key_index(rv_editor_decision_keys, s);
        c.decision = d < rv_editor_decision_keys.size() ? static_cast<rv_editor_decision>(d) : rv_editor_decision::none;
    }
}

void rv_editor_check_field(rv_editor_check &check, const rv_pdklib::rv_manifest_tree_entry &e)
{
    const std::string &s = e.value.str;
    if (e.key == "state") {
        const size_t st = rv_editor_key_index(rv_editor_state_keys, s);
        check.state = st < rv_editor_state_keys.size() ? static_cast<rv_editor_check_state>(st)
                                                        : rv_editor_check_state::not_run;
    } else if (e.key == "note") {
        check.note = s;
    } else if (e.key == "env") {
        check.env = s;
    } else if (e.key == "at") {
        check.at = s;
    } else if (e.key == "hash") {
        check.hash = s;
    }
}

} // namespace

std::filesystem::path rv_editor_candidate_log(const std::filesystem::path &dir, uint32_t number, const std::string &what)
{
    return dir / (std::to_string(number) + "-" + what + ".log");
}

bool rv_editor_candidate_save(const std::filesystem::path &dir, const rv_editor_candidate &c, std::string &error)
{
    const auto q = [](std::string_view s) { return rv_editor_toml_quote(s); };
    std::string t = "# A release candidate of 3dmppc-editor; the editor rewrites this file.\n\n[candidate]\n";
    t += "number = " + std::to_string(c.number) + "\n";
    t += "image = " + q(c.image.string()) + "\n";
    t += "command = " + q(c.command) + "\n";
    t += "built_at = " + q(c.built_at) + "\n";
    t += "burner = " + q(c.burner) + "\n";
    t += "baker = " + q(c.baker) + "\n";
    t += "source_revision = " + q(c.source_revision) + "\n";
    t += "sha256 = " + q(c.sha256) + "\n";
    t += "size = " + std::to_string(c.size) + "\n";
    t += "verified_hash = " + q(c.verified_hash) + "\n";
    t += "verified_at = " + q(c.verified_at) + "\n";
    t += "bytes_changed = " + std::to_string(c.bytes_changed ? 1 : 0) + "\n";
    t += "tree_changed = " + std::to_string(c.tree_changed ? 1 : 0) + "\n";
    t += "decision = " + q(rv_editor_decision_keys[static_cast<size_t>(c.decision)]) + "\n";
    t += "decided_at = " + q(c.decided_at) + "\n";
    t += "operator = " + q(c.operator_name) + "\n";
    t += "playtests = " + std::to_string(c.playtests) + "\n";
    t += "last_run_end = " + q(c.last_run_end) + "\n";
    t += "last_run_clean = " + std::to_string(c.last_run_clean ? 1 : 0) + "\n";
    for (size_t i = 0; i < c.checks.size() && i < rv_editor_check_keys.size(); ++i) {
        const rv_editor_check &check = c.checks[i];
        t += std::string("\n[check.") + rv_editor_check_keys[i] + "]\n";
        t += "state = " + q(rv_editor_state_keys[static_cast<size_t>(check.state)]) + "\n";
        t += "note = " + q(check.note) + "\n";
        t += "env = " + q(check.env) + "\n";
        t += "at = " + q(check.at) + "\n";
        t += "hash = " + q(check.hash) + "\n";
    }
    return rv_editor_file_replace(dir / (std::to_string(c.number) + ".toml"), t, error);
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
            if (section.name == "candidate") {
                for (const auto &e : section.entries) {
                    rv_editor_candidate_field(c, e);
                }
                continue;
            }
            const size_t id = section.name.starts_with("check.")
                ? rv_editor_key_index(rv_editor_check_keys, std::string_view(section.name).substr(6))
                : rv_editor_check_keys.size();
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
