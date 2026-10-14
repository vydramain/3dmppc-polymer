#pragma once
// Findings, Test Case and Observe operations of rv_editor_app: the records, names and requests
// the panes use (rv_editor_app_findings.cpp, rv_editor_app_observe.cpp).

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "app/rv_editor_app_parts.hpp"

namespace rv_editor
{

struct rv_editor_app;

// Findings files: the directory under the project's state dir, and the extensions of the
// text files and logs in it (findings and test results).
constexpr std::string_view dir_name_findings = "findings";
// Test case files (case-N.txt, listed by the Test Case pane), under the project root.
constexpr std::string_view dir_name_testcases = "testcases";
constexpr std::string_view file_ext_text = ".txt";
constexpr std::string_view file_ext_log = ".log";
// File-name stamp "20260926-141203" and the stamp people read, "2026-09-26 14:12:03".
constexpr const char *time_format_file = "%Y%m%d-%H%M%S";
constexpr const char *time_format_people = "%Y-%m-%d %H:%M:%S";
// The name each rv_editor_case_result_kind has in result files (the enum: rv_editor_app_parts.hpp).
constexpr std::string_view case_result_names[] = { "passed", "failed", "blocked" };
static_assert(std::size(case_result_names) == static_cast<size_t>(rv_editor_case_result_kind::count),
    "case result name table size must match enum count");

// A test case file: "title: ..." on its first line, then "steps:" and "expected:"
// each on a line of its own over their text, as a finding writes them.
struct rv_editor_case {
    std::string title;
    std::string steps;
    std::string expected;
};

// Findings (editor/src/app/rv_editor_app_findings.cpp): saves the drafted finding with its log.
// RV_OK, or RV_ERR_IO with the reason in findings.error.
int rv_editor_app_save_finding(rv_editor_app &app);
// Captures the running session's newest frame as a PNG into the findings and attaches it to the
// finding being written. RV_OK, RV_ERR_NOENT when no frame of the live session has arrived,
// RV_ERR_IO when the file cannot be written (reason in findings.error and the log).
int rv_editor_app_capture_frame(rv_editor_app &app);
// Test Case (editor/src/app/rv_editor_app_findings.cpp): reads a case file's title, steps and expected
// into out. RV_OK, or RV_ERR_IO when the file cannot be read as a regular file: missing, not regular,
// open or read failure (reason in error; out is then untouched).
int rv_editor_case_read(const std::filesystem::path &path, rv_editor_case &out, std::string &error);
// The exploratory case: no file, its title only.
rv_editor_case rv_editor_case_exploratory();
// Test Case listing (editor/src/app/rv_editor_app_cases.cpp): the readable cases of testcases/*.txt with their
// text and file time, sorted by path; each unreadable one is logged at this listing. RV_OK, RV_ERR_NOENT when there
// is no directory (not logged: a project may have none), RV_ERR_IO when it cannot be listed (logged). The panel
// asks for it through the cases_list request.
int rv_editor_app_cases_list(rv_editor_app &app);
// Question: the directory changed since the last listing, or a listed or skipped case's file changed or became unreadable.
bool rv_editor_app_cases_stale(const rv_editor_app &app);
// A new case file opened in Code. RV_OK, or RV_ERR_IO with the reason in findings.error and the log.
int rv_editor_app_case_new(rv_editor_app &app);
// A case result with the session, written into the findings. RV_OK, or RV_ERR_IO when it is not
// saved (reason in findings.error and the log). Failed also starts a finding and attaches the live
// session's frame; then the capture's code is returned (its reason in findings.error and the log).
int rv_editor_app_case_result(rv_editor_app &app, const rv_editor_case &c, rv_editor_case_result_kind kind);
// Debug's mark: adds "frame N" with the note (empty: none) to the marks and logs it. RV_OK, or RV_ERR_INVAL
// when no session is live (logged).
int rv_editor_app_add_mark(rv_editor_app &app, const std::string &note);
// Report Issue: captures the live session's frame, logs the report and asks for Findings. RV_OK; RV_ERR_INVAL
// when no session has run (logged as a warning, nothing done). A failed capture is logged by the capture.
int rv_editor_app_report_problem(rv_editor_app &app);
// Clears the finding's error. RV_OK, or RV_ERR_ALREADY when there is none (logged as a warning).
int rv_editor_app_findings_dismiss_error(rv_editor_app &app);
// Text and file helpers the Findings and Test Case panes share: a timestamp by format, the
// session's facts as "key: value" lines, and a file write (error holds the reason).
std::string rv_editor_now_text(const char *format);
std::string rv_editor_session_text(const rv_editor_app &app);
int rv_editor_write_file(const std::filesystem::path &path, const std::string &text, std::string &error);

// Observe (editor/src/app/rv_editor_app_observe.cpp): reads of the session's Lua state. Verbs of the
// requests: keys lists the keys at a path (see src/rv_pconsole/rv_pconsole_cmd_devtools.cpp); get
// reads a value, its path after the trailing space.
constexpr const char *observe_verb_keys = "keys";
constexpr const char *observe_verb_get = "get ";
// The request for the value or the keys at `path`: "get a b", "keys a b".
std::string rv_editor_observe_request(const char *verb, const std::vector<std::string> &path);
// Asks the session for `request`. RV_OK, or the refusal's code (logged: a busy runtime a warning, else an error).
int rv_editor_app_observe_query(rv_editor_app &app, const std::string &request);
// Reads the keys at the path, then each pin; stops at the first refusal. RV_OK, or that refusal's code.
int rv_editor_app_observe_read(rv_editor_app &app);
// Refreshes the session's status, then reads. On a refusal, logs it, marks the frame read and returns
// the code without reading. RV_OK, or the refusal's code.
int rv_editor_app_observe_refresh(rv_editor_app &app);
// Sets the path to the keys in `path` (split at single spaces, empty pieces skipped; empty: the root), then reads.
// RV_OK, or the refusal's code.
int rv_editor_app_observe_open(rv_editor_app &app, const std::string &path);
// Pins the key path `key` ("player x"), then asks its value. RV_ERR_ALREADY when it is pinned already (warned,
// nothing done); else the query's code.
int rv_editor_app_observe_pin(rv_editor_app &app, const std::string &key);
// Drops the pin `key`. RV_OK; RV_ERR_ALREADY when it is not pinned (warned, nothing done).
int rv_editor_app_observe_unpin(rv_editor_app &app, const std::string &key);
// Question: an observe request waits in the queue, or a session query is unanswered until each query is
// answered or overdue.
bool rv_editor_app_observe_waiting(const rv_editor_app &app);

} // namespace rv_editor
