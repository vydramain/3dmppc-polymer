# Fetches the Steam Runtime sysroot tarball, checks its SHA256 pin and extracts it into the sysroot dir.
# Usage: cmake -DRV_SYSROOT_URL=<https://... | file://...> -DRV_SYSROOT_SHA256=<hex> -DRV_SYSROOT_TARBALL=<file>
#              -DRV_SYSROOT_DIR=<dir> -DRV_CURL=<curl> -P cmake/rv_linux_sysroot_download.cmake
# A broken transfer keeps the partial tarball, so the next run resumes it. A complete tarball that matches the pin
# is used as it is. A hash mismatch removes the file. Editing this script does not rerun the download of an
# existing build: delete its download stamp to force it.

if(NOT RV_SYSROOT_URL OR NOT RV_SYSROOT_SHA256 OR NOT RV_SYSROOT_TARBALL OR NOT RV_SYSROOT_DIR OR NOT RV_CURL)
    message(FATAL_ERROR "RV_SYSROOT_URL, RV_SYSROOT_SHA256, RV_SYSROOT_TARBALL, RV_SYSROOT_DIR and RV_CURL must be set")
endif()

# The Steam server answered a Range request over HTTP/1.1 with 206 (checked 2026-10-18); its HTTP/2 transfers
# break mid-way, so HTTP/1.1 is used. --retry-all-errors (curl 7.71+) also retries a dropped connection,
# not only an HTTP status.
set(RV_SYSROOT_CURL_RETRIES 10)
# curl exit 33: the server ignored the Range header, so the partial file cannot be continued.
set(RV_SYSROOT_CURL_NO_RANGE 33)

# A file:// source that is the destination would be removed by a hash mismatch: refuse it before any copy.
if(RV_SYSROOT_URL MATCHES "^file://")
    string(REGEX REPLACE "^file://" "" source "${RV_SYSROOT_URL}")
    get_filename_component(source_real "${source}" REALPATH)
    get_filename_component(tarball_real "${RV_SYSROOT_TARBALL}" REALPATH)
    if("${source_real}" STREQUAL "${tarball_real}")
        message(FATAL_ERROR "the file:// source is the tarball destination ${tarball_real}: "
                "point RV_LINUX_SYSROOT_URL at another copy of the tarball.")
    endif()
endif()

# Fetches the tarball into RV_SYSROOT_TARBALL. A partial file is resumed by curl.
function(rv_sysroot_fetch)
    if(RV_SYSROOT_URL MATCHES "^file://")
        string(REGEX REPLACE "^file://" "" source "${RV_SYSROOT_URL}")
        file(COPY_FILE "${source}" "${RV_SYSROOT_TARBALL}" RESULT rc)
        if(NOT "${rc}" STREQUAL "0")
            message(FATAL_ERROR "cannot copy ${source} to ${RV_SYSROOT_TARBALL}: ${rc}")
        endif()
        return()
    endif()
    if(NOT RV_SYSROOT_URL MATCHES "^https?://")
        message(FATAL_ERROR "unsupported sysroot URL (want https://, http:// or file://): ${RV_SYSROOT_URL}")
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
            --retry ${RV_SYSROOT_CURL_RETRIES}
            --retry-all-errors
            --output "${RV_SYSROOT_TARBALL}"
            "${RV_SYSROOT_URL}"
        RESULT_VARIABLE rc)
    if(rc EQUAL RV_SYSROOT_CURL_NO_RANGE)
        file(REMOVE "${RV_SYSROOT_TARBALL}")
        message(FATAL_ERROR "the server ignored the Range request of ${RV_SYSROOT_URL}, so the partial file cannot resume: "
                "it is removed, the next build restarts from zero.")
    endif()
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "download of ${RV_SYSROOT_URL} failed (curl exit ${rc}): "
                "the partial file ${RV_SYSROOT_TARBALL} is kept; the next build resumes it.")
    endif()
endfunction()

# file(SHA256) gives lowercase hex; the pin is compared case-insensitively.
string(TOLOWER "${RV_SYSROOT_SHA256}" expected)
set(actual "")
if(EXISTS "${RV_SYSROOT_TARBALL}")
    file(SHA256 "${RV_SYSROOT_TARBALL}" actual)
endif()
if(NOT "${actual}" STREQUAL "${expected}")
    rv_sysroot_fetch()
    file(SHA256 "${RV_SYSROOT_TARBALL}" actual)
    if(NOT "${actual}" STREQUAL "${expected}")
        file(REMOVE "${RV_SYSROOT_TARBALL}")
        message(FATAL_ERROR "SHA256 of ${RV_SYSROOT_URL} is ${actual}, expected ${expected}: "
                "the partial file is removed, the next build downloads it again.")
    endif()
endif()

# A fresh tree replaces any earlier one, so no file from an older tarball survives.
file(REMOVE_RECURSE "${RV_SYSROOT_DIR}")
file(MAKE_DIRECTORY "${RV_SYSROOT_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${RV_SYSROOT_TARBALL}"
    WORKING_DIRECTORY "${RV_SYSROOT_DIR}"
    RESULT_VARIABLE rc
    ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "cannot extract ${RV_SYSROOT_TARBALL} into ${RV_SYSROOT_DIR} (exit ${rc}): ${err}")
endif()

# The tree is the product; the 1.2 GB tarball is removed once it is extracted.
file(REMOVE "${RV_SYSROOT_TARBALL}")
message(STATUS "sysroot extracted: ${RV_SYSROOT_DIR}")
