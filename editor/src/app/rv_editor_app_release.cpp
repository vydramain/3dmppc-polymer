// Release commands: a candidate is the build job writing an image, and its
// playtest is the same session running that image.

#include "app/rv_editor_app.hpp"

#include "release/rv_editor_candidate_store.hpp"
#include "project/rv_editor_toml.hpp"
#include "text/rv_editor_text.hpp"

#include <charconv>
#include <chrono>
#include <fstream>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// The lookup of the running candidate build's sources; the record waits for it.
rv_editor_revision_job g_revision;

// Directory name for cached release candidates.
constexpr std::string_view candidates_dir_name = "candidates";

// Disc image file extension.
constexpr std::string_view disc_image_ext = ".mppcdisc";

// Memory card file extension.
constexpr std::string_view memory_card_ext = ".mppccard";

// Candidate build log file name.
constexpr std::string_view candidate_log_name_build = "build";

std::filesystem::path rv_editor_candidates_dir(const rv_editor_app &app)
{
    return app.project.cache_dir / candidates_dir_name;
}

// One more than the highest <n>.mppcdisc already there: no image is written over.
uint32_t rv_editor_candidate_next(const std::filesystem::path &dir)
{
    uint32_t top = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != disc_image_ext) {
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
    return std::string("[") + rv_editor_log_source_name(line.source) + "] " + rv_editor_log_level_code(line.level) + " " +
        line.text + "\n";
}

// The log lines seq `from`..`to` (0: to the end), restricted to the session when supplied.
std::string rv_editor_lines(const rv_editor_log &log, uint64_t from, uint64_t to, const rv_editor_session *session = nullptr)
{
    std::string out;
    if (!log.lines().empty() && log.lines().front().seq > from) {
        out += "(lines before this were dropped from the editor's bounded log)\n";
    }
    for (const rv_editor_log_line &line : log.lines()) {
        const bool own = session == nullptr || (line.run == session->number() && line.pid == session->pid());
        if (own && line.seq >= from && (to == 0 || line.seq <= to) && line.source != rv_editor_log_source::protocol) {
            out += rv_editor_line_text(line);
        }
    }
    return out;
}

bool g_build_ended = false;

bool rv_editor_playtest_image_unchanged(const rv_editor_playtest &playtest)
{
    struct stat current{};
    const auto &original = playtest.image_stat;
    return ::stat(playtest.image.c_str(), &current) == 0 && current.st_dev == original.st_dev &&
        current.st_ino == original.st_ino && current.st_size == original.st_size &&
        current.st_mtim.tv_sec == original.st_mtim.tv_sec && current.st_mtim.tv_nsec == original.st_mtim.tv_nsec &&
        current.st_ctim.tv_sec == original.st_ctim.tv_sec && current.st_ctim.tv_nsec == original.st_ctim.tv_nsec;
}

void rv_editor_playtest_check_set(rv_editor_candidate &c,
    const rv_editor_playtest &playtest,
    rv_editor_check_state state,
    const std::string &note,
    const std::string &env)
{
    rv_editor_check_set(c, rv_editor_check_loads, state, note, env);
    c.checks[rv_editor_check_loads].hash = playtest.hash;
}

} // namespace

int rv_editor_app_release_finish_build(rv_editor_app &app)
{
    if (!app.release.building) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "build not finished: no candidate is being built");
        return RV_ERR_INVAL;
    }
    rv_editor_release &r = app.release;
    g_build_ended = false;
    r.building = false;
    r.building_revision = g_revision.text();
    if (app.build.state() != rv_editor_build_state::succeeded) {
        r.last_failure = "Candidate #" + std::to_string(r.building_number) + " was not made: the build " +
            rv_editor_build_state_name(app.build.state()) + ". Nothing below is new.";
        return RV_ERR_NOENT;
    }
    r.last_failure.clear();
    rv_editor_candidate c;
    c.number = r.building_number;
    c.image = app.build.image();
    c.command = app.tools.burner.path.string() + " build " + app.project.root.string() + " -o " + c.image.string() +
        " --baker " + app.tools.baker.path.string();
    c.built_at = rv_editor_wall_clock();
    c.burner = r.building_burner;
    c.baker = r.building_baker;
    c.tree_changed = r.tree_changed_during;
    c.source_revision = r.building_revision;
    c.checks = rv_editor_checks_make();
    std::string error;
    const auto log_path =
        rv_editor_candidate_log(rv_editor_candidates_dir(app), c.number, std::string(candidate_log_name_build));
    if (rv_editor_file_replace(log_path, rv_editor_lines(app.log, r.build_first_seq, app.log.revision()), error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "build log not kept: " + error);
    }
    c.dirty = true;
    rv_editor_candidate_hash(c);
    r.candidates.push_back(std::move(c));
    r.selected = r.candidates.size() - 1;
    return RV_OK;
}

bool rv_editor_app_release_build_noted()
{
    return g_build_ended;
}

int rv_editor_app_release_note_build_end(rv_editor_app &app)
{
    if (!app.release.building) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "build end not noted: no candidate is being built");
        return RV_ERR_INVAL;
    }
    g_build_ended = true;
    return RV_OK;
}

int rv_editor_app_release_revision_poll(rv_editor_app &app)
{
    if (!app.release.building) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "revision not polled: no candidate is being built");
        return RV_ERR_INVAL;
    }
    return g_revision.poll() ? RV_OK : RV_ERR_PENDING;
}

int rv_editor_app_build_candidate(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        rv_editor_app_build_alert_refused(app, why);
        return RV_ERR_BUSY;
    }
    // A candidate records the tools' versions; a check still running would leave them empty.
    if (app.tools.burner.version_check.running() || rv_editor_bakers_checking(app.tools)) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "not built: tool versions are still being checked");
        return RV_ERR_BUSY;
    }
    rv_editor_release &r = app.release;
    const std::filesystem::path dir = rv_editor_candidates_dir(app);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const uint32_t number = rv_editor_candidate_next(dir);
    std::string error;
    const auto first_seq = app.log.revision() + 1;
    const int err =
        app.build.start(app.project, app.tools, app.log, error, dir / (std::to_string(number) + std::string(disc_image_ext)));
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build a candidate: " + error);
        rv_editor_app_build_alert_refused(app, error);
        return err;
    }
    rv_editor_app_build_alert_clear(app);
    app.build_first_seq = first_seq;
    r.build_first_seq = first_seq;
    r.building = true;
    r.building_number = number;
    // No check runs here (refused above), so the versions are final.
    r.building_burner = app.tools.burner.version;
    r.building_baker = rv_editor_bakers_version_text(app.tools);
    r.tree_changed_during = false;
    g_build_ended = false;
    g_revision.start(app.project.root);
    return RV_OK;
}

const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        return rv_editor_text("app_release.no_candidate");
    }
    const rv_editor_candidate &c = r.candidates[r.selected];
    if (c.bytes_changed) {
        return rv_editor_text("app_release.image_bytes_changed");
    }
    if (c.sha256.empty() || c.hashing.valid()) {
        return rv_editor_text("app_release.hashing_image");
    }
    if (app.session.live()) {
        return rv_editor_text("app_release.session_running");
    }
    if (r.playing >= 0) {
        return rv_editor_text("app_release.finishing_playtest");
    }
    if (!app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    return nullptr;
}

int rv_editor_app_run_candidate(rv_editor_app &app)
{
    rv_editor_app_release_update_playtest(app);
    const char *why = rv_editor_app_why_not_run_candidate(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    rv_editor_release &r = app.release;
    rv_editor_candidate &c = r.candidates[r.selected];
    rv_editor_playtest playtest;
    playtest.candidate = c.number;
    playtest.image = c.image;
    playtest.hash = c.sha256;
    if (::stat(playtest.image.c_str(), &playtest.image_stat) != 0 || !S_ISREG(playtest.image_stat.st_mode)) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot read candidate image: " + c.image.string());
        return RV_ERR_IO;
    }
    // Read the image asynchronously and wait for its hash before setting a successful verdict.
    rv_editor_candidate_hash(c);
    // Its own memory card, so no save of the development builds reaches it.
    std::filesystem::path card = c.image;
    card.replace_extension(memory_card_ext);
    playtest.first_seq = app.log.revision() + 1;
    const int err = rv_editor_app_run_session(app, rv_editor_artifact{ c.image, c.number }, card);
    if (err != RV_OK) {
        return err;
    }
    playtest.session = app.session.number();
    playtest.console = app.session.console();
    rv_editor_playtest_check_set(c,
        playtest,
        rv_editor_check_state::running,
        "session #" + std::to_string(playtest.session),
        playtest.console.string());
    r.playing = static_cast<int>(r.selected);
    r.playtest = std::move(playtest);
    return RV_OK;
}

int rv_editor_app_release_save_candidate(rv_editor_app &app, rv_editor_candidate &c)
{
    std::string error;
    const int err = rv_editor_candidate_save(rv_editor_candidates_dir(app), c, error);
    // Logged once per failure run: the flag stays set until a save goes through.
    const bool first_failure = err != RV_OK && !c.save_failed;
    c.save_failed = err != RV_OK;
    if (first_failure) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "candidate record not kept, trying again: " + error);
    }
    if (err != RV_OK) {
        return err;
    }
    c.dirty = false;
    return RV_OK;
}

bool rv_editor_app_release_still_playing(const rv_editor_app &app)
{
    const auto &release = app.release;
    return static_cast<size_t>(release.playing) < release.candidates.size() && app.session.live() &&
        release.playtest.session == app.session.number() && release.playtest.candidate == app.session.build_number() &&
        release.playtest.image == app.session.disc_dir();
}

void rv_editor_app_release_update_playtest(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.playing < 0 || static_cast<size_t>(r.playing) >= r.candidates.size()) {
        return;
    }
    rv_editor_candidate &c = r.candidates[static_cast<size_t>(r.playing)];
    auto &playtest = r.playtest;
    if (c.number != playtest.candidate || c.image != playtest.image || c.sha256 != playtest.hash) {
        r.playing = -1;
        r.playtest = {};
        return;
    }

    const rv_editor_check &loads = c.checks[rv_editor_check_loads];
    const rv_editor_session &s = app.session;
    const bool still_playing = rv_editor_app_release_still_playing(app);
    if (!still_playing && !playtest.ended) {
        auto &ended = playtest.ended.emplace();
        if (s.number() == playtest.session) {
            ended.facts = s.facts();
            ended.reason = s.end_reason();
            ended.log = rv_editor_lines(app.log, playtest.first_seq, app.log.revision(), &s);
            ended.clean = s.state() == rv_editor_run_state::exited && !ended.reason.starts_with(end_reason_force_stopped);
        } else {
            ended.reason = "a different session started before this one closed";
        }
    }
    const auto &facts = playtest.ended ? playtest.ended->facts : s.facts();
    const auto env = [&] {
        return playtest.console.string() + (!facts.pdk.empty() ? ", PDK " + facts.pdk : "");
    };
    // A replacement or write voids the result, including a verdict set before the change.
    if (loads.state != rv_editor_check_state::blocked && (c.bytes_changed || !rv_editor_playtest_image_unchanged(playtest))) {
        rv_editor_playtest_check_set(c,
            playtest,
            rv_editor_check_state::blocked,
            "candidate image changed during playtest",
            env());
        rv_editor_candidate_hash(c);
    }
    const bool answered = still_playing ? s.connected() : facts.at != std::chrono::system_clock::time_point{};
    if (loads.state == rv_editor_check_state::running && answered) {
        if (c.hashing.valid()) {
            return;
        }
        if (c.verified_hash == playtest.hash) {
            const bool image = facts.medium == medium_fixed;
            rv_editor_playtest_check_set(c,
                playtest,
                image ? rv_editor_check_state::passed : rv_editor_check_state::failed,
                image ? "mounted as an image, disc " + facts.disc : "the console did not mount it as an image",
                env());
        }
    }
    if (still_playing || c.hashing.valid()) {
        return;
    }

    // If an earlier read overlapped a replacement, verify the current file after that read finishes.
    const bool image_changed = loads.state == rv_editor_check_state::blocked;
    if (image_changed && !c.bytes_changed) {
        rv_editor_candidate_hash(c);
    }
    const auto &ended = *playtest.ended;
    if (loads.state == rv_editor_check_state::running) {
        rv_editor_playtest_check_set(c, playtest, rv_editor_check_state::failed, ended.reason, env());
    }
    ++c.playtests;
    std::string error;
    if (rv_editor_file_replace(
            rv_editor_candidate_log(rv_editor_candidates_dir(app), c.number, "playtest-" + std::to_string(c.playtests)),
            ended.log,
            error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "playtest log not kept: " + error);
    }
    c.dirty = true;
    c.last_run_end = ended.reason;
    c.last_run_clean = ended.clean && !image_changed;
    r.playing = -1;
    r.playtest = {};
}

void rv_editor_app_release_changed(rv_editor_app &app)
{
    for (rv_editor_candidate &c : app.release.candidates) {
        c.tree_changed = true;
    }
    app.release.tree_changed_during = app.release.building;
}

int rv_editor_app_export_report(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "no release report: there is no candidate");
        return RV_ERR_NOENT;
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
    t += c.decision == rv_editor_decision::approved ? "approved by " + c.operator_name + " at " + c.decided_at :
        c.decision == rv_editor_decision::rejected  ? "rejected by " + c.operator_name + " at " + c.decided_at :
        rv_editor_why_not_approve(c) == nullptr     ? std::string("awaiting approval") :
                                                      std::string("not ready");
    t += "\n";
    // The logs kept beside the record, so a report written days later still has them.
    const std::filesystem::path dir = rv_editor_candidates_dir(app);
    const std::filesystem::path build_log = rv_editor_candidate_log(dir, c.number, std::string(candidate_log_name_build));
    t += "\n--- build log (" + build_log.string() + ") ---\n" + rv_editor_file_text(build_log);
    for (uint32_t i = 1; i <= c.playtests; ++i) {
        const std::filesystem::path log = rv_editor_candidate_log(dir, c.number, "playtest-" + std::to_string(i));
        t += "\n--- playtest " + std::to_string(i) + " log (" + log.string() + ") ---\n" + rv_editor_file_text(log);
    }
    const std::filesystem::path path =
        rv_editor_candidates_dir(app) / (std::to_string(c.number) + "-report-" + std::to_string(std::time(nullptr)) + ".txt");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << t;
    out.close();
    if (!out) {
        r.error = "cannot write " + path.string();
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, r.error);
        return RV_ERR_IO;
    }
    r.error.clear();
    r.report = path.string();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "release report written: " + path.string());
    return RV_OK;
}

} // namespace rv_editor
