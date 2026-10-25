# Loads one pinned file: fetches RV_FETCH_URL into RV_FETCH_FILE and checks its SHA256 against RV_FETCH_SHA256.
# With RV_FETCH_DIR set, the file is also extracted there and then removed.
# Usage: cmake -DRV_FETCH_URL=<https://... | http://... | file://...> -DRV_FETCH_SHA256=<hex> -DRV_FETCH_FILE=<file>
#              [-DRV_FETCH_DIR=<dir>] [-DRV_FETCH_PATHS=<path;path...>] [-DRV_FETCH_NAME=<phrase>]
#              [-DRV_FETCH_BASE=<dir>] [-DRV_CURL=<curl>] -P cmake/rv_fetch.cmake
# A broken transfer keeps the partial file, so the next run resumes it. A file in place that does not match the pin is
# removed and fetched from zero, not resumed, when the server copy is not longer than it (HEAD Content-Length; an unknown
# size keeps the resume), since no resume can finish it. A shorter file is resumed; if the server refuses the resume
# (curl exit 33), it is removed and fetched from zero. If the file still does not match after the resume, it is removed
# and fetched from zero. A download from zero that does not match is only a warning: the file is used as it is. With
# RV_FETCH_DIR the file is extracted and then removed; without it the file is kept, and a file that matches the pin is
# not fetched again. A failed extraction removes the archive and fetches it again from zero, unless this run already
# fetched from zero; a second failure is an error. A run makes at most one download from zero. The stamp in RV_FETCH_DIR
# holds the pinned SHA256 (also on mismatch) and, with
# RV_FETCH_PATHS, one line per path. A matching stamp skips the run without fetching or looking for the file, and removes
# a leftover archive. A missing or other stamp replaces the tree. The tree is built in a sibling dir and renamed into
# place only when complete.
# RV_FETCH_PATHS (needs RV_FETCH_DIR) extracts only the listed archive paths; each must exist in the archive. The stamp
# then lists the paths after the SHA256, so a partial tree is replaced by a full one and the reverse.
# Everything that touches RV_FETCH_FILE runs under the lock <RV_FETCH_FILE>.lock, held until the run ends, so two builds
# sharing a cache file do not resume each other's partial download. Other files do not wait. The stamp check is before it.

include(${CMAKE_CURRENT_LIST_DIR}/rv_log.cmake)

if(NOT RV_FETCH_URL OR NOT RV_FETCH_SHA256 OR NOT RV_FETCH_FILE)
    message(FATAL_ERROR "RV_FETCH_URL, RV_FETCH_SHA256 and RV_FETCH_FILE must be set")
endif()
if(NOT RV_FETCH_URL MATCHES "^(https?|file)://")
    message(FATAL_ERROR "unsupported URL (want https://, http:// or file://): ${RV_FETCH_URL}")
endif()
if(RV_FETCH_PATHS AND NOT RV_FETCH_DIR)
    message(FATAL_ERROR "RV_FETCH_PATHS extracts into RV_FETCH_DIR: set RV_FETCH_DIR too")
endif()

# Absolute paths: tar runs in the work dir, and a trailing slash in RV_FETCH_DIR would put that work dir inside the tree.
get_filename_component(RV_FETCH_FILE "${RV_FETCH_FILE}" ABSOLUTE)
if(RV_FETCH_DIR)
    get_filename_component(RV_FETCH_DIR "${RV_FETCH_DIR}" ABSOLUTE)
endif()
# The header line: a short phrase from the caller, else the file.
if(NOT RV_FETCH_NAME)
    set(RV_FETCH_NAME "${RV_FETCH_FILE}")
endif()

# The Steam server answered a Range request over HTTP/1.1 with 206 (checked 2026-10-18); its HTTP/2 transfers
# break mid-way, so HTTP/1.1 is used. --retry-all-errors (curl 7.71+) also retries a dropped connection,
# not only an HTTP status.
set(RV_FETCH_CURL_RETRIES 10)
# curl exit 33: the server ignored the Range header, so the partial file cannot be continued.
set(RV_FETCH_CURL_NO_RANGE 33)
# The stamp in RV_FETCH_DIR: the pinned SHA256 (RV_FETCH_SHA256), not the hash of the archive, which may differ after
# a warning; with RV_FETCH_PATHS, one more line per path (see rv_fetch_stamp).
set(RV_FETCH_STAMP .rv_fetch_sha256)

# A file:// source that is the destination would be deleted after extraction: refuse it before any copy.
if(RV_FETCH_URL MATCHES "^file://")
    string(REGEX REPLACE "^file://" "" source "${RV_FETCH_URL}")
    get_filename_component(source_real "${source}" REALPATH)
    get_filename_component(file_real "${RV_FETCH_FILE}" REALPATH)
    if("${source_real}" STREQUAL "${file_real}")
        message(FATAL_ERROR "the file:// source is the destination ${file_real}: "
                "point RV_FETCH_URL at another copy of the file.")
    endif()
endif()

# Sets <out> to <path> as the log shows it: relative to RV_FETCH_BASE when inside it, else absolute.
# Without RV_FETCH_BASE the path is shown as it is.
function(rv_fetch_show out path)
    if(RV_FETCH_BASE)
        rv_log_relpath(rel "${path}" "${RV_FETCH_BASE}")
        set(${out} "${rel}" PARENT_SCOPE)
    else()
        set(${out} "${path}" PARENT_SCOPE)
    endif()
endfunction()

# Sets <out> to the curl program: RV_CURL, else the one on PATH.
function(rv_fetch_curl_path out)
    if(NOT RV_CURL)
        find_program(RV_CURL curl)
    endif()
    if(NOT RV_CURL)
        message(FATAL_ERROR "curl is not found: it fetches ${RV_FETCH_URL}; install curl or pass -DRV_CURL=<curl>")
    endif()
    set(${out} "${RV_CURL}" PARENT_SCOPE)
endfunction()

# Sets <out> to the exit code of one curl run into RV_FETCH_FILE; the run resumes a partial file.
function(rv_fetch_transfer out)
    rv_fetch_curl_path(curl)
    # --continue-at - resumes the partial file; --fail turns an HTTP error into a failed exit code.
    # --progress-bar draws one progress line instead of curl's table, which breaks up in a log file.
    execute_process(
        COMMAND "${curl}"
            --http1.1
            --fail
            --location
            --show-error
            --progress-bar
            --continue-at -
            --retry ${RV_FETCH_CURL_RETRIES}
            --retry-all-errors
            --output "${RV_FETCH_FILE}"
            "${RV_FETCH_URL}"
        RESULT_VARIABLE rc)
    set(${out} "${rc}" PARENT_SCOPE)
endfunction()

# Fetches RV_FETCH_URL into RV_FETCH_FILE. A partial file is resumed by curl. If the server ignores the Range request,
# the file is removed and fetched again from zero in this run. <restarted_out> is TRUE when the fetch started from zero:
# a file:// copy always does, and so does a restart.
function(rv_fetch_download restarted_out)
    set(${restarted_out} FALSE PARENT_SCOPE)
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    rv_log_step("fetching ${RV_FETCH_URL} into ${shown_file}")
    if(RV_FETCH_URL MATCHES "^file://")
        string(REGEX REPLACE "^file://" "" source "${RV_FETCH_URL}")
        file(COPY_FILE "${source}" "${RV_FETCH_FILE}" RESULT rc)
        if(NOT "${rc}" STREQUAL "0")
            message(FATAL_ERROR "cannot copy ${source} to ${RV_FETCH_FILE}: ${rc}")
        endif()
        set(${restarted_out} TRUE PARENT_SCOPE)
        return()
    endif()
    rv_fetch_transfer(rc)
    if(rc EQUAL RV_FETCH_CURL_NO_RANGE)
        file(REMOVE "${RV_FETCH_FILE}")
        rv_log_step("the server ignored the Range request: the partial file is removed, fetched again from zero")
        set(${restarted_out} TRUE PARENT_SCOPE)
        rv_fetch_transfer(rc)
    endif()
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "download of ${RV_FETCH_URL} failed (curl exit ${rc}): "
                "the partial file ${RV_FETCH_FILE} is kept; the next run resumes it.")
    endif()
endfunction()

# Sets <out> to the Content-Length of the final response of a HEAD request (after redirects), or "" when it has none.
function(rv_fetch_server_size out)
    set(${out} "" PARENT_SCOPE)
    rv_fetch_curl_path(curl)
    execute_process(
        COMMAND "${curl}" --http1.1 --head --location --fail --silent --show-error "${RV_FETCH_URL}"
        OUTPUT_VARIABLE headers
        RESULT_VARIABLE rc)
    if(NOT "${rc}" STREQUAL "0")
        return()
    endif()
    rv_fetch_final_length(length "${headers}")
    set(${out} "${length}" PARENT_SCOPE)
endfunction()

# Sets <out> to the Content-Length of the last response in <text> (the headers of one HEAD run), or "" when it has none.
# Every response starts with a status line, which resets the length; a header name counts only at the start of a line.
function(rv_fetch_final_length out text)
    string(REPLACE "\r" "" text "${text}")
    string(TOLOWER "${text}" text)
    string(REPLACE "\n" ";" lines "${text}")
    set(length "")
    foreach(line IN LISTS lines)
        if("${line}" MATCHES "^http/")
            set(length "")
        elseif("${line}" MATCHES "^content-length: *([0-9]+)")
            set(length "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out} "${length}" PARENT_SCOPE)
endfunction()

# Removes RV_FETCH_FILE and sets <out> TRUE when the file is not shorter than the server copy: resuming it cannot finish
# (the Steam server answers a range past the end with 416, which curl retries for minutes). Unknown size: resume as before.
function(rv_fetch_drop_whole out)
    set(${out} FALSE PARENT_SCOPE)
    if(RV_FETCH_URL MATCHES "^file://")
        return()
    endif()
    if(NOT EXISTS "${RV_FETCH_FILE}")
        return()
    endif()
    rv_fetch_server_size(server)
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    if("${server}" STREQUAL "")
        rv_log_step("the server size of ${RV_FETCH_URL} is unknown: ${shown_file} is resumed")
        return()
    endif()
    file(SIZE "${RV_FETCH_FILE}" local)
    if("${local}" LESS "${server}")
        return()
    endif()
    rv_log_step("${shown_file} is ${local} bytes, not shorter than the server copy (${server}): removed, fetched from zero")
    file(REMOVE "${RV_FETCH_FILE}")
    set(${out} TRUE PARENT_SCOPE)
endfunction()

# The stamp text: the pinned SHA256, then one line per RV_FETCH_PATHS entry. Without a trailing newline.
function(rv_fetch_stamp out)
    string(TOLOWER "${RV_FETCH_SHA256}" text)
    foreach(path IN LISTS RV_FETCH_PATHS)
        string(APPEND text "\n${path}")
    endforeach()
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Extracts RV_FETCH_FILE into RV_FETCH_DIR through a sibling dir, then writes the stamp and renames it into place.
# Sets <out> to the tar failure, or to "" on success; a failed extraction removes the archive. The caller decides on a retry.
# RV_FETCH_PATHS unquoted: each entry is one member path for tar. tar fails on a path missing from the archive.
function(rv_fetch_extract out)
    rv_fetch_stamp(stamp)
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    rv_fetch_show(shown_dir "${RV_FETCH_DIR}")
    rv_log_step("extracting ${shown_file} into ${shown_dir}: this can take a while")
    set(work "${RV_FETCH_DIR}.part")
    # Leftovers of an interrupted run.
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${RV_FETCH_FILE}" ${RV_FETCH_PATHS}
        WORKING_DIRECTORY "${work}"
        RESULT_VARIABLE rc
        ERROR_VARIABLE err)
    if(NOT "${rc}" STREQUAL "0")
        file(REMOVE "${RV_FETCH_FILE}")
        set(${out} "exit ${rc}: ${err}" PARENT_SCOPE)
        return()
    endif()
    file(WRITE "${work}/${RV_FETCH_STAMP}" "${stamp}\n")
    # The old tree goes only after the new one is complete, so a failed extraction keeps it.
    file(REMOVE_RECURSE "${RV_FETCH_DIR}")
    file(RENAME "${work}" "${RV_FETCH_DIR}")
    rv_log_step("extracted ${shown_file} into ${shown_dir}")
    set(${out} "" PARENT_SCOPE)
endfunction()

# Warns when RV_FETCH_FILE does not match the pin; <actual> is its SHA256. The file is used as it is.
function(rv_fetch_warn_mismatch actual)
    string(TOLOWER "${RV_FETCH_SHA256}" expected)
    if("${actual}" STREQUAL "${expected}")
        return()
    endif()
    message(WARNING "SHA256 of ${RV_FETCH_URL} is ${actual}, expected ${expected}: "
            "the file ${RV_FETCH_FILE} is used as it is: with RV_FETCH_DIR set it is extracted and then removed; "
            "without RV_FETCH_DIR it is kept.")
endfunction()

# Takes the lock of RV_FETCH_FILE for the rest of the run (GUARD PROCESS). A busy lock is waited for without a timeout.
# Called at most once per run: a second file(LOCK) in the same process releases the lock.
# The .lock file is never removed: after a release a waiter would lock the old file while a newcomer locks a new one.
function(rv_fetch_lock)
    set(lock "${RV_FETCH_FILE}.lock")
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    file(LOCK "${lock}" GUARD PROCESS TIMEOUT 0 RESULT_VARIABLE result)
    if("${result}" STREQUAL "0")
        return()
    endif()
    rv_log_step("waiting for another build fetching ${shown_file}")
    file(LOCK "${lock}" GUARD PROCESS RESULT_VARIABLE result)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "cannot lock ${lock}: ${result}")
    endif()
endfunction()

# Sets <out> TRUE when RV_FETCH_DIR holds the tree of this pin and paths (the stamp matches). Reads the stamp only.
function(rv_fetch_stamp_matches out)
    set(${out} FALSE PARENT_SCOPE)
    if(NOT RV_FETCH_DIR)
        return()
    endif()
    rv_fetch_stamp(stamp)
    set(recorded "")
    if(EXISTS "${RV_FETCH_DIR}/${RV_FETCH_STAMP}")
        file(READ "${RV_FETCH_DIR}/${RV_FETCH_STAMP}" recorded)
        string(STRIP "${recorded}" recorded)
    endif()
    if("${recorded}" STREQUAL "${stamp}")
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(rv_fetch_log_extracted)
    rv_fetch_show(shown_dir "${RV_FETCH_DIR}")
    rv_log_step("already extracted into ${shown_dir}: nothing is downloaded")
endfunction()

function(rv_fetch_main)
    rv_log_header("${RV_FETCH_NAME}")
    string(TOLOWER "${RV_FETCH_SHA256}" expected)
    # Unlocked fast path: a matching stamp with no archive left touches no cache file.
    rv_fetch_stamp_matches(matched)
    if(matched AND NOT EXISTS "${RV_FETCH_FILE}")
        rv_fetch_log_extracted()
        return()
    endif()
    rv_fetch_lock()
    # The stamp is read again under the lock: a waiting run may have seen another build extract the tree.
    rv_fetch_stamp_matches(matched)
    if(matched)
        # A run that died between the rename and the removal leaves the archive behind.
        file(REMOVE "${RV_FETCH_FILE}")
        rv_fetch_log_extracted()
        return()
    endif()
    # file(SHA256) gives lowercase hex; the pin is compared case-insensitively.
    set(actual "")
    set(existed FALSE)
    if(EXISTS "${RV_FETCH_FILE}")
        set(existed TRUE)
        file(SHA256 "${RV_FETCH_FILE}" actual)
    endif()
    # fresh: this run has fetched from zero (no file before, a whole file dropped, or a restart); a run allows one.
    set(fresh FALSE)
    if("${actual}" STREQUAL "${expected}")
        rv_fetch_show(shown_file "${RV_FETCH_FILE}")
        rv_log_step("taken from the cache ${shown_file}: SHA256 matches, no download")
    else()
        # rv_fetch_drop_whole resets its out: TRUE only when it removed a whole file.
        rv_fetch_drop_whole(fresh)
        rv_fetch_download(restarted)
        if(NOT existed OR restarted)
            set(fresh TRUE)
        endif()
        file(SHA256 "${RV_FETCH_FILE}" actual)
    endif()
    # Only a resumed file gets a second try: a download from zero that does not match stays a warning.
    if(existed AND NOT fresh AND NOT "${actual}" STREQUAL "${expected}")
        rv_fetch_show(shown_file "${RV_FETCH_FILE}")
        rv_log_step("${shown_file} does not match the pin after the resume: removed, fetched again from zero")
        file(REMOVE "${RV_FETCH_FILE}")
        rv_fetch_download(unused)
        file(SHA256 "${RV_FETCH_FILE}" actual)
        set(fresh TRUE)
    endif()
    rv_fetch_warn_mismatch("${actual}")
    if(RV_FETCH_DIR)
        rv_fetch_extract(failure)
        # A broken archive gets one download from zero, unless this run already made one.
        if(NOT "${failure}" STREQUAL "" AND NOT fresh)
            rv_log_step("extraction of ${RV_FETCH_FILE} failed (${failure}): fetched again from zero")
            rv_fetch_download(unused)
            file(SHA256 "${RV_FETCH_FILE}" actual)
            rv_fetch_warn_mismatch("${actual}")
            rv_fetch_extract(failure)
        endif()
        if(NOT "${failure}" STREQUAL "")
            message(FATAL_ERROR "cannot extract ${RV_FETCH_FILE} into ${RV_FETCH_DIR} (${failure}): "
                    "the archive is removed from the cache; "
                    "a RV_FETCH_PATHS entry missing from the archive is named above.")
        endif()
        file(REMOVE "${RV_FETCH_FILE}")
    endif()
endfunction()

rv_fetch_main()
