# Loads one pinned file: fetches RV_FETCH_URL into RV_FETCH_FILE and checks its SHA256 against RV_FETCH_SHA256.
# With RV_FETCH_DIR set, the file is also extracted there and then removed.
# Usage: cmake -DRV_FETCH_URL=<https://... | http://... | file://...> -DRV_FETCH_SHA256=<hex> -DRV_FETCH_FILE=<file>
#              [-DRV_FETCH_DIR=<dir>] [-DRV_FETCH_NAME=<phrase>] [-DRV_FETCH_BASE=<dir>]
#              [-DRV_CURL=<curl>] -P cmake/rv_fetch.cmake
# A broken transfer keeps the partial file, so the next run resumes it. A hash mismatch is only a warning: the file is
# used as it is. With RV_FETCH_DIR the file is extracted and then removed; without it the file is kept, and a file that
# matches the pin is not fetched again. The stamp in RV_FETCH_DIR holds the pinned SHA256 (also on mismatch): a matching
# stamp skips the run without fetching or looking for the file, and removes a leftover archive. A missing or other stamp
# replaces the tree. The tree is built in a sibling dir and renamed into place only when complete.

include(${CMAKE_CURRENT_LIST_DIR}/rv_log.cmake)

if(NOT RV_FETCH_URL OR NOT RV_FETCH_SHA256 OR NOT RV_FETCH_FILE)
    message(FATAL_ERROR "RV_FETCH_URL, RV_FETCH_SHA256 and RV_FETCH_FILE must be set")
endif()
if(NOT RV_FETCH_URL MATCHES "^(https?|file)://")
    message(FATAL_ERROR "unsupported URL (want https://, http:// or file://): ${RV_FETCH_URL}")
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
# The stamp in RV_FETCH_DIR: the pinned SHA256 (RV_FETCH_SHA256), not the hash of the archive, which may differ after a warning.
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

# Fetches RV_FETCH_URL into RV_FETCH_FILE. A partial file is resumed by curl.
function(rv_fetch_download)
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    rv_log_step("fetching ${RV_FETCH_URL} into ${shown_file}")
    if(RV_FETCH_URL MATCHES "^file://")
        string(REGEX REPLACE "^file://" "" source "${RV_FETCH_URL}")
        file(COPY_FILE "${source}" "${RV_FETCH_FILE}" RESULT rc)
        if(NOT "${rc}" STREQUAL "0")
            message(FATAL_ERROR "cannot copy ${source} to ${RV_FETCH_FILE}: ${rc}")
        endif()
        return()
    endif()
    if(NOT RV_CURL)
        find_program(RV_CURL curl)
    endif()
    if(NOT RV_CURL)
        message(FATAL_ERROR "curl is not found: it fetches ${RV_FETCH_URL}; install curl or pass -DRV_CURL=<curl>")
    endif()
    # --continue-at - resumes the partial file; --fail turns an HTTP error into a failed exit code.
    # --progress-bar draws one progress line instead of curl's table, which breaks up in a log file.
    execute_process(
        COMMAND "${RV_CURL}"
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
    if(rc EQUAL RV_FETCH_CURL_NO_RANGE)
        file(REMOVE "${RV_FETCH_FILE}")
        message(FATAL_ERROR "the server ignored the Range request of ${RV_FETCH_URL}, so the partial file cannot resume: "
                "it is removed, the next run restarts from zero.")
    endif()
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "download of ${RV_FETCH_URL} failed (curl exit ${rc}): "
                "the partial file ${RV_FETCH_FILE} is kept; the next run resumes it.")
    endif()
endfunction()

# Extracts RV_FETCH_FILE into RV_FETCH_DIR through a sibling dir, then writes the stamp and renames it into place.
function(rv_fetch_extract)
    string(TOLOWER "${RV_FETCH_SHA256}" expected)
    rv_fetch_show(shown_file "${RV_FETCH_FILE}")
    rv_fetch_show(shown_dir "${RV_FETCH_DIR}")
    rv_log_step("extracting ${shown_file} into ${shown_dir}: this can take a while")
    set(work "${RV_FETCH_DIR}.part")
    # Leftovers of an interrupted run.
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${RV_FETCH_FILE}"
        WORKING_DIRECTORY "${work}"
        RESULT_VARIABLE rc
        ERROR_VARIABLE err)
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "cannot extract ${RV_FETCH_FILE} into ${RV_FETCH_DIR} (exit ${rc}): ${err}\n"
                "if the archive is broken, remove ${RV_FETCH_FILE} to download it again.")
    endif()
    file(WRITE "${work}/${RV_FETCH_STAMP}" "${expected}\n")
    # The old tree goes only after the new one is complete, so a failed extraction keeps it.
    file(REMOVE_RECURSE "${RV_FETCH_DIR}")
    file(RENAME "${work}" "${RV_FETCH_DIR}")
    rv_log_step("extracted ${shown_file} into ${shown_dir}")
endfunction()

function(rv_fetch_main)
    rv_log_header("${RV_FETCH_NAME}")
    string(TOLOWER "${RV_FETCH_SHA256}" expected)
    if(RV_FETCH_DIR)
        set(recorded "")
        if(EXISTS "${RV_FETCH_DIR}/${RV_FETCH_STAMP}")
            file(READ "${RV_FETCH_DIR}/${RV_FETCH_STAMP}" recorded)
            string(STRIP "${recorded}" recorded)
        endif()
        if("${recorded}" STREQUAL "${expected}")
            # A run that died between the rename and the removal leaves the archive behind.
            file(REMOVE "${RV_FETCH_FILE}")
            rv_fetch_show(shown_dir "${RV_FETCH_DIR}")
            rv_log_step("already extracted into ${shown_dir}: nothing is downloaded")
            return()
        endif()
    endif()
    # file(SHA256) gives lowercase hex; the pin is compared case-insensitively.
    set(actual "")
    if(EXISTS "${RV_FETCH_FILE}")
        file(SHA256 "${RV_FETCH_FILE}" actual)
    endif()
    if("${actual}" STREQUAL "${expected}")
        rv_fetch_show(shown_file "${RV_FETCH_FILE}")
        rv_log_step("taken from the cache ${shown_file}: SHA256 matches, no download")
    else()
        rv_fetch_download()
        file(SHA256 "${RV_FETCH_FILE}" actual)
    endif()
    if(NOT "${actual}" STREQUAL "${expected}")
        message(WARNING "SHA256 of ${RV_FETCH_URL} is ${actual}, expected ${expected}: "
                "the file ${RV_FETCH_FILE} is used as it is: with RV_FETCH_DIR set it is extracted and then removed; "
                "without RV_FETCH_DIR it is kept.")
    endif()
    if(RV_FETCH_DIR)
        rv_fetch_extract()
        file(REMOVE "${RV_FETCH_FILE}")
    endif()
endfunction()

rv_fetch_main()
