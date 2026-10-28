#pragma once

// The report file of mppcinspector's run: cleared before the rules run, written after them.
// Kept as a header so another program can write reports under the same contract in its own process.

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"

namespace rv_pdktools
{

namespace detail
{

// Prefixes of the report errors, not the short-write reason below: the path or the reason follows each one, as given.
inline constexpr std::string_view rv_report_write_text = "cannot write report ";
inline constexpr std::string_view rv_report_remove_text = "cannot remove old report ";
inline constexpr std::string_view rv_report_look_text = "cannot examine report ";
inline constexpr std::string_view rv_report_not_regular_text = "not a regular file: ";
inline constexpr std::string_view rv_report_partial_remove_text = "; cannot remove the partial report: ";
// errno MAY be 0 after a short fwrite: glibc sets it, the standard does not promise either way. strerror(0) is "Success".
inline constexpr std::string_view rv_report_short_write_text = "short write";

// Code 0: errno was not set. A short write MAY leave it 0 (see rv_report_short_write_text).
inline int rv_report_error(const std::filesystem::path &report, int code, std::string &error)
{
    const std::string reason = code == 0 ? std::string(rv_report_short_write_text) : std::string(std::strerror(code));
    error = std::string(rv_report_write_text) + report.string() + ": " + reason;
    return RV_ERR_IO;
}

// A failed write or close leaves no partial report, unless its removal fails; then that failure is added to `error` and the
// partial report stays.
inline int rv_report_fail_partial(const std::filesystem::path &report, int code, std::string &error)
{
    const int rc = rv_report_error(report, code, error);
    std::error_code ec;
    std::filesystem::remove(report, ec);
    if (ec) {
        error += std::string(rv_report_partial_remove_text) + ec.message();
    }
    return rc;
}

} // namespace detail

// Call before the rules run, so a failed run leaves no old report. Absent is RV_OK; a regular file is removed.
// A link, a directory or a FIFO is RV_ERR_INVAL, refused before anything is changed. RV_ERR_IO: the path cannot
// be examined or the removal fails, and `error` names the file.
inline int rv_inspector_report_clear(const std::filesystem::path &report, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::symlink_status(report, ec);
    const bool missing = st.type() == std::filesystem::file_type::not_found || ec == std::errc::no_such_file_or_directory;
    const bool absent = missing || ec == std::errc::not_a_directory;
    if (absent) {
        return RV_OK;
    }
    if (ec) {
        error = std::string(detail::rv_report_look_text) + report.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (st.type() != std::filesystem::file_type::regular) {
        error = std::string(detail::rv_report_not_regular_text) + report.string();
        return RV_ERR_INVAL;
    }
    std::filesystem::remove(report, ec);
    if (ec) {
        error = std::string(detail::rv_report_remove_text) + report.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Each call replaces `report` with the findings, even with none (an empty file is the clean answer). The path is not
// checked: fopen follows a symbolic link. The caller clears the old report first with rv_inspector_report_clear, which
// refuses a link. A failed open writes nothing. A failed write or close leaves no partial report, unless its removal
// fails too: then `error` names that failure and the partial report stays. Any failure is RV_ERR_IO.
inline int rv_inspector_report_write(const std::filesystem::path &report,
    std::span<const rv_pdklib::rv_inspect_finding> findings,
    std::string &error)
{
    const std::string text = rv_pdklib::rv_inspect_report_format(findings);
    std::FILE *file = std::fopen(report.string().c_str(), "wb");
    if (file == nullptr) {
        return detail::rv_report_error(report, errno, error);
    }
    errno = 0;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const int write_code = errno;
    if (written != text.size()) {
        std::fclose(file);
        return detail::rv_report_fail_partial(report, write_code, error);
    }
    if (std::fclose(file) != 0) {
        return detail::rv_report_fail_partial(report, errno, error);
    }
    return RV_OK;
}

} // namespace rv_pdktools
