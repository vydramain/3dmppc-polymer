// Release commands: a candidate is the build job writing an image, and its
// playtest is the same session running that image.

#include "app/rv_editor_app.hpp"

#include "release/rv_editor_candidate_store.hpp"
#include "project/rv_editor_toml.hpp"

#include <charconv>
#include <fstream>
#include <system_error>

namespace rv_editor
{

namespace
{

std::filesystem::path rv_editor_candidates_dir(const rv_editor_app &app)
{
    return app.project.cache_dir / "candidates";
}

// One more than the highest <n>.mppcdisc already there: no image is written over.
uint32_t rv_editor_candidate_next(const std::filesystem::path &dir)
{
    uint32_t top = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".mppcdisc") {
            continue;
        }
        const std::string stem = it->path().stem().string();
        uint32_t n = 0;
        const auto [ptr, err] = std::from_chars(stem.data(), stem.data() + stem.size(), n);
        if (err == std::errc{} && ptr == stem.data() + stem.size()) {
            top = std::max(top, n);
        }
    }
    return top + 1;
}

std::string rv_editor_line_text(const rv_editor_log_line &line)
{
    const char *level = line.level == rv_editor_log_level::error ? "ERR"
        : line.level == rv_editor_log_level::warning             ? "WRN"
                                                                 : "INF";
    return std::string("[") + rv_editor_log_source_name(line.source) + "] " + level + " " + line.text + "\n";
}

// The log lines seq `from`..`to` (0: to the end), and a note when older ones were dropped.
std::string rv_editor_lines(const rv_editor_log &log, uint64_t from, uint64_t to)
{
    std::string out;
    if (!log.lines().empty() && log.lines().front().seq > from) {
        out += "(lines before this were dropped from the editor's bounded log)\n";
    }
    for (const rv_editor_log_line &line : log.lines()) {
        if (line.seq >= from && (to == 0 || line.seq <= to) && line.source != rv_editor_log_source::protocol) {
            out += rv_editor_line_text(line);
        }
    }
    return out;
}

void rv_editor_candidate_finish_build(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    r.building = false;
    if (app.build.state() != rv_editor_build_state::succeeded) {
        r.last_failure = "Candidate #" + std::to_string(r.building_number) + " was not made: the build " +
            rv_editor_build_state_name(app.build.state()) + ". Nothing below is new.";
        return;
    }
    r.last_failure.clear();
    rv_editor_candidate c;
    c.number = r.building_number;
    c.image = app.build.image();
    c.command = app.tools.burner.path.string() + " build " + app.project.root.string() + " -o " + c.image.string() +
        " --baker " + app.tools.baker.path.string();
    c.built_at = rv_editor_wall_clock();
    c.burner = app.tools.burner.version;
    c.baker = app.tools.baker.version;
    c.tree_changed = r.tree_changed_during;
    c.source_revision = r.building_revision;
    c.checks = rv_editor_checks_make();
    std::string error;
    if (!rv_editor_file_replace(rv_editor_candidate_log(rv_editor_candidates_dir(app), c.number, "build"),
            rv_editor_lines(app.log, r.build_first_seq, app.log.revision()), error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "build log not kept: " + error);
    }
    c.dirty = true;
    rv_editor_candidate_hash(c);
    r.candidates.push_back(std::move(c));
    r.selected = r.candidates.size() - 1;
}

} // namespace

void rv_editor_app_build_candidate(rv_editor_app &app)
{
    if (rv_editor_app_why_not_build(app) != nullptr) {
        return;
    }
    rv_editor_release &r = app.release;
    const std::filesystem::path dir = rv_editor_candidates_dir(app);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const uint32_t number = rv_editor_candidate_next(dir);
    std::string error;
    r.build_first_seq = app.log.revision() + 1;
    app.build_first_seq = r.build_first_seq;
    if (!app.build.start(app.project, app.tools, app.log, error, dir / (std::to_string(number) + ".mppcdisc"))) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build a candidate: " + error);
        return;
    }
    r.building = true;
    r.building_number = number;
    r.tree_changed_during = false;
    // ponytail: git runs on the UI thread, at most 3 s each; a job of its own if that shows.
    r.building_revision = rv_editor_source_revision(app.project.root);
}

const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        return "No candidate yet: Build Candidate first";
    }
    const rv_editor_candidate &c = r.candidates[r.selected];
    if (c.bytes_changed) {
        return "The image's bytes changed after it was built";
    }
    if (c.sha256.empty()) {
        return "Hashing the image first";
    }
    if (app.session.live()) {
        return "A session is running: Stop it first";
    }
    if (!app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    return nullptr;
}

void rv_editor_app_run_candidate(rv_editor_app &app)
{
    if (rv_editor_app_why_not_run_candidate(app) != nullptr) {
        return;
    }
    rv_editor_release &r = app.release;
    rv_editor_candidate &c = r.candidates[r.selected];
    // The bytes are read again while it runs; a difference voids the results.
    rv_editor_candidate_hash(c);
    // Its own memory card, so no save of the development builds reaches it.
    std::filesystem::path card = c.image;
    card.replace_extension(".mppccard");
    r.playtest_first_seq = app.log.revision() + 1;
    if (!rv_editor_app_start(app, rv_editor_artifact{ c.image, c.number }, card)) {
        return;
    }
    r.playing = static_cast<int>(r.selected);
    rv_editor_check_set(c, rv_editor_check_loads, rv_editor_check_state::running, "session #" +
        std::to_string(app.session.number()), app.tools.console.path.string());
}

void rv_editor_app_release_update(rv_editor_app &app, bool build_ended)
{
    rv_editor_release &r = app.release;
    rv_editor_app_player_update(app);
    if (build_ended && r.building) {
        rv_editor_candidate_finish_build(app);
    }
    for (rv_editor_candidate &c : r.candidates) {
        rv_editor_candidate_poll(c);
        // Written as soon as it changes: a closed window loses nothing (DAT-03).
        if (!c.dirty) {
            continue;
        }
        std::string error;
        if (rv_editor_candidate_save(rv_editor_candidates_dir(app), c, error)) {
            c.dirty = false;
            c.save_failed = false;
        } else if (!c.save_failed) {
            c.save_failed = true;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
                "candidate record not kept, trying again: " + error);
        }
    }
    if (r.playing < 0 || static_cast<size_t>(r.playing) >= r.candidates.size()) {
        return;
    }
    rv_editor_candidate &c = r.candidates[static_cast<size_t>(r.playing)];
    rv_editor_check &loads = c.checks[rv_editor_check_loads];
    const rv_editor_session &s = app.session;
    const std::string env = app.tools.console.path.string() + ", PDK " + s.facts().pdk;
    if (loads.state == rv_editor_check_state::running && s.connected()) {
        // Mounted as an image (medium fixed) and answering: the claim of this check, no more.
        const bool image = s.facts().medium == "fixed";
        rv_editor_check_set(c, rv_editor_check_loads, image ? rv_editor_check_state::passed : rv_editor_check_state::failed,
            image ? "mounted as an image, disc " + s.facts().disc : "the console did not mount it as an image", env);
    }
    if (s.live()) {
        return;
    }
    if (loads.state == rv_editor_check_state::running) {
        rv_editor_check_set(c, rv_editor_check_loads, rv_editor_check_state::failed, s.end_reason(), env);
    }
    ++c.playtests;
    std::string error;
    if (!rv_editor_file_replace(
            rv_editor_candidate_log(rv_editor_candidates_dir(app), c.number, "playtest-" + std::to_string(c.playtests)),
            rv_editor_lines(app.log, r.playtest_first_seq, app.log.revision()), error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "playtest log not kept: " + error);
    }
    c.dirty = true;
    c.last_run_end = s.end_reason();
    c.last_run_clean = s.state() == rv_editor_run_state::exited && s.end_reason().rfind("force", 0) != 0;
    r.playing = -1;
}

void rv_editor_app_release_changed(rv_editor_app &app)
{
    for (rv_editor_candidate &c : app.release.candidates) {
        c.tree_changed = true;
    }
    app.release.tree_changed_during = app.release.building;
}

void rv_editor_app_export_report(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        return;
    }
    rv_editor_candidate &c = r.candidates[r.selected];
    std::string t = "3dmppc-editor release report\nwritten: " + rv_editor_wall_clock() + "\n\n";
    t += "project: " + app.project.root.string() + "\n";
    t += "candidate: #" + std::to_string(c.number) + ", built " + c.built_at + "\n";
    t += "image: " + c.image.string() + "\n";
    t += "sha256: " + c.sha256 + " (" + std::to_string(c.size) + " bytes)\n";
    t += "last verified: " + c.verified_at + (c.verified_hash == c.sha256 ? ", same bytes" : ", DIFFERENT bytes") +
        (c.bytes_changed ? "; results void" : "") + "\n";
    t += "built by: " + c.command + "\n";
    t += "burner: " + c.burner + "\nbaker: " + c.baker + "\n";
    t += "runtime: " + app.tools.console.path.string() + " (development console)\n";
    t += "source revision: " + (c.source_revision.empty() ? std::string("not recorded") : c.source_revision) + "\n";
    t += std::string("sources: ") + (c.tree_changed ? "changed after the build started" : "no change seen") + "\n\n";
    t += "checks (all required; Skipped does not count as Passed):\n";
    for (const rv_editor_check &check : c.checks) {
        t += "- " + std::string(check.name) + ": " + rv_editor_check_state_name(check.state) +
            (check.state != rv_editor_check_state::not_run && !rv_editor_check_valid(c, check) ? " (void: other bytes)" : "") +
            (check.at.empty() ? "" : ", " + check.at) + (check.env.empty() ? "" : ", on " + check.env) +
            (check.note.empty() ? "" : "; " + check.note) + "\n  passed means: " + check.passes_when + "\n";
    }
    t += "\n" + rv_editor_checks_summary(c) + "\n";
    const bool void_decision = c.bytes_changed && c.decision != rv_editor_decision::none;
    t += std::string("decision: ") + (void_decision ? "void (other bytes); was " : "");
    t += c.decision == rv_editor_decision::approved ? "approved by " + c.operator_name + " at " + c.decided_at
        : c.decision == rv_editor_decision::rejected ? "rejected by " + c.operator_name + " at " + c.decided_at
        : rv_editor_why_not_approve(c) == nullptr    ? std::string("awaiting approval")
                                                     : std::string("not ready");
    t += "\n";
    // The logs kept beside the record, so a report written days later still has them.
    const std::filesystem::path dir = rv_editor_candidates_dir(app);
    t += "\n--- build log (" + rv_editor_candidate_log(dir, c.number, "build").string() + ") ---\n" +
        rv_editor_file_text(rv_editor_candidate_log(dir, c.number, "build"));
    for (uint32_t i = 1; i <= c.playtests; ++i) {
        const std::filesystem::path log = rv_editor_candidate_log(dir, c.number, "playtest-" + std::to_string(i));
        t += "\n--- playtest " + std::to_string(i) + " log (" + log.string() + ") ---\n" + rv_editor_file_text(log);
    }
    const std::filesystem::path path = rv_editor_candidates_dir(app) /
        (std::to_string(c.number) + "-report-" + std::to_string(std::time(nullptr)) + ".txt");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << t;
    out.close();
    if (!out) {
        r.error = "cannot write " + path.string();
        return;
    }
    r.error.clear();
    r.report = path.string();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "release report written: " + path.string());
}

} // namespace rv_editor
