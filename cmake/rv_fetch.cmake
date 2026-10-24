# Loads one pinned file: fetches RV_FETCH_URL into RV_FETCH_FILE and checks its SHA256 against RV_FETCH_SHA256.
# With RV_FETCH_DIR set, the file is also extracted there.
# Usage: cmake -DRV_FETCH_URL=<https://... | http://... | file://...> -DRV_FETCH_SHA256=<hex> -DRV_FETCH_FILE=<file>
#              [-DRV_FETCH_DIR=<dir>] [-DRV_CURL=<curl>] -P cmake/rv_fetch.cmake
# A broken transfer keeps the partial file, so the next run resumes it. A file that matches the pin is kept and is
# never fetched again. A hash mismatch removes the file. RV_FETCH_DIR holds a stamp with the SHA256 it was extracted
# from: a matching stamp skips the run; a missing or other stamp replaces the tree. The tree is built in a sibling dir and
# renamed into place only when complete.

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

# The Steam server answered a Range request over HTTP/1.1 with 206 (checked 2026-10-18); its HTTP/2 transfers
# break mid-way, so HTTP/1.1 is used. --retry-all-errors (curl 7.71+) also retries a dropped connection,
# not only an HTTP status.
set(RV_FETCH_CURL_RETRIES 10)
# curl exit 33: the server ignored the Range header, so the partial file cannot be continued.
set(RV_FETCH_CURL_NO_RANGE 33)
# LLVM 23.1.4's .tar.zst has a 1 GiB window (zstd -lv: Window Size 1.000 GiB); without --long=30 zstd refuses it
# with "Window size larger than maximum".
set(RV_FETCH_ZSTD_LONG 30)
# The extraction stamp in RV_FETCH_DIR: the SHA256 the tree was extracted from.
set(RV_FETCH_STAMP .rv_fetch_sha256)

# A file:// source that is the destination would be removed by a hash mismatch: refuse it before any copy.
if(RV_FETCH_URL MATCHES "^file://")
    string(REGEX REPLACE "^file://" "" source "${RV_FETCH_URL}")
    get_filename_component(source_real "${source}" REALPATH)
    get_filename_component(file_real "${RV_FETCH_FILE}" REALPATH)
    if("${source_real}" STREQUAL "${file_real}")
        message(FATAL_ERROR "the file:// source is the destination ${file_real}: "
                "point RV_FETCH_URL at another copy of the file.")
    endif()
endif()

# Fetches RV_FETCH_URL into RV_FETCH_FILE. A partial file is resumed by curl.
function(rv_fetch_download)
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
    # Progress stays on the terminal, so a 1.2 GB transfer does not look hung.
    execute_process(
        COMMAND "${RV_CURL}"
            --http1.1
            --fail
            --location
            --show-error
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
    set(work "${RV_FETCH_DIR}.part")
    set(tar "${work}.tar")
    set(source "${RV_FETCH_FILE}")
    # Leftovers of an interrupted run.
    file(REMOVE_RECURSE "${work}" "${tar}")
    file(MAKE_DIRECTORY "${work}")
    if(RV_FETCH_FILE MATCHES "\\.tar\\.zst$")
        find_program(RV_FETCH_ZSTD zstd)
        if(NOT RV_FETCH_ZSTD)
            message(FATAL_ERROR "zstd is not found: it decompresses ${RV_FETCH_FILE}; install zstd")
        endif()
        execute_process(
            COMMAND "${RV_FETCH_ZSTD}" -d -f --long=${RV_FETCH_ZSTD_LONG} "${RV_FETCH_FILE}" -o "${tar}"
            RESULT_VARIABLE rc
            ERROR_VARIABLE err)
        if(NOT "${rc}" STREQUAL "0")
            message(FATAL_ERROR "cannot decompress ${RV_FETCH_FILE} (zstd exit ${rc}): ${err}")
        endif()
        set(source "${tar}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${source}"
        WORKING_DIRECTORY "${work}"
        RESULT_VARIABLE rc
        ERROR_VARIABLE err)
    file(REMOVE "${tar}")
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "cannot extract ${source} into ${RV_FETCH_DIR} (exit ${rc}): ${err}")
    endif()
    file(WRITE "${work}/${RV_FETCH_STAMP}" "${expected}\n")
    # The old tree goes only after the new one is complete, so a failed extraction keeps it.
    file(REMOVE_RECURSE "${RV_FETCH_DIR}")
    file(RENAME "${work}" "${RV_FETCH_DIR}")
    message(STATUS "extracted ${RV_FETCH_FILE} into ${RV_FETCH_DIR}")
endfunction()

function(rv_fetch_main)
    string(TOLOWER "${RV_FETCH_SHA256}" expected)
    if(RV_FETCH_DIR)
        set(recorded "")
        if(EXISTS "${RV_FETCH_DIR}/${RV_FETCH_STAMP}")
            file(READ "${RV_FETCH_DIR}/${RV_FETCH_STAMP}" recorded)
            string(STRIP "${recorded}" recorded)
        endif()
        if("${recorded}" STREQUAL "${expected}")
            message(STATUS "${RV_FETCH_DIR} is already extracted from ${expected}")
            return()
        endif()
    endif()
    # file(SHA256) gives lowercase hex; the pin is compared case-insensitively.
    set(actual "")
    if(EXISTS "${RV_FETCH_FILE}")
        file(SHA256 "${RV_FETCH_FILE}" actual)
    endif()
    if(NOT "${actual}" STREQUAL "${expected}")
        rv_fetch_download()
        file(SHA256 "${RV_FETCH_FILE}" actual)
        if(NOT "${actual}" STREQUAL "${expected}")
            file(REMOVE "${RV_FETCH_FILE}")
            message(FATAL_ERROR "SHA256 of ${RV_FETCH_URL} is ${actual}, expected ${expected}: "
                    "the file is removed, the next run downloads it again.")
        endif()
    endif()
    if(RV_FETCH_DIR)
        rv_fetch_extract()
    endif()
endfunction()

rv_fetch_main()
