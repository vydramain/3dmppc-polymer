# Lays the SDL patch set of one pinned tag onto the SDL source before it is built.
# The identity is the tag, the version, the archive SHA256, this script's SHA256 and one "<sha256> <path>" line per patch
# and copied file. The stamp (.rv-sdl3-patch-set in the source) holds the identity of the set laid onto it: an equal stamp
# changes nothing. Any other set is laid onto a source restored from the pinned archive, so the result never depends on
# what an earlier set or a hand edit left. The restore swaps RV_SDL3_SRC for the extracted tree and writes a provisional
# stamp ("laying", never an identity) before any patch. A source with a stamp was laid by this script, so it is replaced
# without the version guard; a source without one must be an SDL source of the pinned version. A checkout (.git) is
# refused in either case.
# A run that dies is repaired by the next run: an aside source is put back, leftover aside and staging dirs are removed,
# and the stamp is written last.
# Patches (*.patch directly in the set dir) are applied in name order, one git apply per patch. The other files are copied
# to the same relative path when their content differs. Names starting with '.' or ending in '~' (any path part) are not
# part of the set. Any change of the set restores the source, so a file removed from the set is gone after the next run.
# Usage: cmake -DRV_SDL3_SRC=<sdl source dir> -DRV_SDL3_PATCH_DIR=<cmake/sdl3/<tag>> -DRV_SDL3_VERSION=<3.4.18>
#              -DRV_SDL3_TAG=<release-3.4.18> -DRV_SDL3_ARCHIVE=<SDL3-3.4.18.tar.gz> -DRV_SDL3_SHA256=<hex>
#              -P cmake/rv_sdl3_patch.cmake

include(${CMAKE_CURRENT_LIST_DIR}/rv_log.cmake)

# Sets <out> to MAJOR.MINOR.MICRO of the SDL_version.h at <header>; "" when the file or a macro is missing.
function(rv_sdl3_read_version out header)
    set(${out} "" PARENT_SCOPE)
    if(NOT EXISTS "${header}")
        return()
    endif()
    file(READ "${header}" text)
    set(parts "")
    foreach(part MAJOR MINOR MICRO)
        string(REGEX MATCH "#define SDL_${part}_VERSION[ \t]+([0-9]+)" match "${text}")
        if("${match}" STREQUAL "")
            return()
        endif()
        list(APPEND parts "${CMAKE_MATCH_1}")
    endforeach()
    list(JOIN parts "." version)
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

# Removes the staging dir, then fails: every failure once the archive is extracted goes through here.
function(rv_sdl3_fail_staged text)
    file(REMOVE_RECURSE "${staging}")
    message(FATAL_ERROR "${text}")
endfunction()

foreach(param RV_SDL3_SRC RV_SDL3_PATCH_DIR RV_SDL3_VERSION RV_SDL3_TAG RV_SDL3_ARCHIVE RV_SDL3_SHA256)
    if("${${param}}" STREQUAL "")
        message(FATAL_ERROR "${param} must be set")
    endif()
endforeach()

get_filename_component(RV_SDL3_SRC "${RV_SDL3_SRC}" ABSOLUTE)
get_filename_component(RV_SDL3_PATCH_DIR "${RV_SDL3_PATCH_DIR}" ABSOLUTE)
get_filename_component(RV_SDL3_ARCHIVE "${RV_SDL3_ARCHIVE}" ABSOLUTE)
string(TOLOWER "${RV_SDL3_SHA256}" RV_SDL3_SHA256)

# Paths under the source would be replaced with it, and the source would be replaced with its own set.
foreach(name RV_SDL3_PATCH_DIR RV_SDL3_ARCHIVE)
    file(RELATIVE_PATH rel_path "${RV_SDL3_SRC}" "${${name}}")
    file(RELATIVE_PATH src_in "${${name}}" "${RV_SDL3_SRC}")
    if("${rel_path}" STREQUAL "" OR NOT rel_path MATCHES "^(\\.\\.$|\\.\\./|/)")
        message(FATAL_ERROR "${name} (${${name}}) lies in RV_SDL3_SRC (${RV_SDL3_SRC}): "
                "the source is replaced, keep it outside")
    endif()
    if("${src_in}" STREQUAL "" OR NOT src_in MATCHES "^(\\.\\.$|\\.\\./|/)")
        message(FATAL_ERROR "RV_SDL3_SRC (${RV_SDL3_SRC}) lies in ${name} (${${name}}): "
                "the source is replaced, keep it outside")
    endif()
endforeach()

# The stamp records the identity of the set laid onto the source; it is written last, so a run that dies restores again.
set(stamp_name ".rv-sdl3-patch-set")
set(stamp "${RV_SDL3_SRC}/${stamp_name}")
# The archive is extracted here first; a leftover from an interrupted run is the script's own.
set(staging "${RV_SDL3_SRC}.rv-sdl3-extract")
# The source is moved here during the swap; a run that dies in between leaves it, and the next run puts it back.
set(aside "${RV_SDL3_SRC}.rv-sdl3-aside")

if(NOT IS_DIRECTORY "${RV_SDL3_PATCH_DIR}")
    message(FATAL_ERROR "no SDL patch set for ${RV_SDL3_TAG}: ${RV_SDL3_PATCH_DIR} does not exist")
endif()

find_program(RV_SDL3_GIT git)
if(NOT RV_SDL3_GIT)
    message(FATAL_ERROR "git is not found: it applies the SDL patches of ${RV_SDL3_TAG}")
endif()
# The source dir may sit inside the project's work tree: git apply must not resolve paths against that repository.
get_filename_component(source_parent "${RV_SDL3_SRC}" DIRECTORY)
set(ENV{GIT_CEILING_DIRECTORIES} "${source_parent}")

file(GLOB_RECURSE entries LIST_DIRECTORIES FALSE RELATIVE "${RV_SDL3_PATCH_DIR}" "${RV_SDL3_PATCH_DIR}/*")
list(FILTER entries EXCLUDE REGEX "(^|/)\\.|~(/|$)")
set(patches "")
set(files "")
foreach(rel IN LISTS entries)
    if(NOT rel MATCHES "\\.patch$")
        list(APPEND files "${rel}")
    elseif(rel MATCHES "/")
        message(FATAL_ERROR "${rel} in ${RV_SDL3_PATCH_DIR}: a patch must sit directly in the set dir (${RV_SDL3_TAG})")
    else()
        list(APPEND patches "${rel}")
    endif()
endforeach()
list(SORT patches)
list(SORT files)
if(NOT patches AND NOT files)
    message(FATAL_ERROR "SDL patch set ${RV_SDL3_TAG} is empty: ${RV_SDL3_PATCH_DIR} has no patch and no file")
endif()

# A copied file may not be a patch target: the copy would replace the patched result.
foreach(rel IN LISTS files)
    foreach(patch IN LISTS patches)
        file(STRINGS "${RV_SDL3_PATCH_DIR}/${patch}" headers REGEX "^(---|\\+\\+\\+) [ab]/")
        foreach(line IN LISTS headers)
            if("${line}" STREQUAL "--- a/${rel}" OR "${line}" STREQUAL "+++ b/${rel}")
                message(FATAL_ERROR "${rel} is copied from the set but ${patch} patches it (SDL ${RV_SDL3_TAG})")
            endif()
        endforeach()
    endforeach()
endforeach()

set(lines "")
foreach(rel IN LISTS patches files)
    file(SHA256 "${RV_SDL3_PATCH_DIR}/${rel}" hash)
    list(APPEND lines "${hash} ${rel}")
endforeach()
list(SORT lines)
list(JOIN lines "\n" listing)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" script_hash)
set(identity "${RV_SDL3_TAG}\n${RV_SDL3_VERSION}\n${RV_SDL3_SHA256}\n${script_hash}\n${listing}\n")

# Undo an interrupted run before the stamp is read: an aside source with no source is put back; with both present the
# aside is left by a swap that finished, and it is removed. A staging dir is the leftover of an extraction.
if(NOT IS_DIRECTORY "${RV_SDL3_SRC}" AND IS_DIRECTORY "${aside}")
    file(RENAME "${aside}" "${RV_SDL3_SRC}")
    rv_log_step("${RV_SDL3_SRC} was moved to ${aside} by an interrupted run: put back")
elseif(IS_DIRECTORY "${RV_SDL3_SRC}" AND IS_DIRECTORY "${aside}")
    file(REMOVE_RECURSE "${aside}")
    rv_log_step("${aside} was left by a finished swap: removed")
endif()
if(IS_DIRECTORY "${staging}")
    file(REMOVE_RECURSE "${staging}")
    rv_log_step("${staging} was left by an interrupted run: removed")
endif()

set(recorded "")
set(stamped FALSE)
if(EXISTS "${stamp}")
    set(stamped TRUE)
    file(READ "${stamp}" recorded)
endif()
if("${recorded}" STREQUAL "${identity}")
    rv_log_step("SDL ${RV_SDL3_TAG} patch set is already applied in ${RV_SDL3_SRC}: nothing is rewritten")
    return()
endif()

# A checkout is never replaced, stamped or not. A laid tree never holds .git (the archive has none).
if(EXISTS "${RV_SDL3_SRC}/.git")
    message(FATAL_ERROR "${RV_SDL3_SRC} holds .git: it is a checkout and is not replaced")
endif()

# A source without a stamp was not laid by this script: it must be an SDL source of the pinned version.
if(NOT stamped)
    if(NOT EXISTS "${RV_SDL3_SRC}/include/SDL3/SDL_version.h")
        message(FATAL_ERROR "${RV_SDL3_SRC} is not an SDL source (include/SDL3/SDL_version.h is missing): it is not replaced")
    endif()
    rv_sdl3_read_version(source_version "${RV_SDL3_SRC}/include/SDL3/SDL_version.h")
    if("${source_version}" STREQUAL "")
        message(FATAL_ERROR "${RV_SDL3_SRC} has no SDL_MAJOR/MINOR/MICRO_VERSION: it is not replaced")
    endif()
    if(NOT "${source_version}" STREQUAL "${RV_SDL3_VERSION}")
        message(FATAL_ERROR "${RV_SDL3_SRC} is SDL ${source_version}, the patch set ${RV_SDL3_TAG} is for "
                "${RV_SDL3_VERSION}: the dir is not replaced")
    endif()
endif()

# Restore the pristine source from the pinned archive. Nothing is removed before the archive is verified and extracted.
if(NOT EXISTS "${RV_SDL3_ARCHIVE}")
    message(FATAL_ERROR "the pinned SDL archive is missing: ${RV_SDL3_ARCHIVE}")
endif()
file(SHA256 "${RV_SDL3_ARCHIVE}" archive_hash)
if(NOT "${archive_hash}" STREQUAL "${RV_SDL3_SHA256}")
    message(FATAL_ERROR "SHA256 of ${RV_SDL3_ARCHIVE} is ${archive_hash}, the pin of SDL ${RV_SDL3_TAG} is ${RV_SDL3_SHA256}")
endif()

file(REMOVE_RECURSE "${staging}")
file(MAKE_DIRECTORY "${staging}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${RV_SDL3_ARCHIVE}"
    WORKING_DIRECTORY "${staging}"
    RESULT_VARIABLE rc
    ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
    rv_sdl3_fail_staged("cannot extract ${RV_SDL3_ARCHIVE}: ${err}")
endif()
file(GLOB tops RELATIVE "${staging}" "${staging}/*")
list(LENGTH tops top_count)
if(NOT top_count EQUAL 1 OR NOT IS_DIRECTORY "${staging}/${tops}")
    rv_sdl3_fail_staged("${RV_SDL3_ARCHIVE} must hold exactly one top-level dir (SDL ${RV_SDL3_TAG})")
endif()
set(staged "${staging}/${tops}")

rv_sdl3_read_version(archive_version "${staged}/include/SDL3/SDL_version.h")
if("${archive_version}" STREQUAL "")
    rv_sdl3_fail_staged("${RV_SDL3_ARCHIVE} has no SDL_MAJOR/MINOR/MICRO_VERSION (SDL ${RV_SDL3_TAG})")
endif()
if(NOT "${archive_version}" STREQUAL "${RV_SDL3_VERSION}")
    rv_sdl3_fail_staged("${RV_SDL3_ARCHIVE} holds SDL ${archive_version}, the patch set ${RV_SDL3_TAG} is for "
                        "${RV_SDL3_VERSION}")
endif()

file(RENAME "${RV_SDL3_SRC}" "${aside}")
file(RENAME "${staged}" "${RV_SDL3_SRC}")
file(REMOVE_RECURSE "${aside}")
file(REMOVE_RECURSE "${staging}")
# Provisional: never equal to an identity, so a run that dies while patching is restored by the next run.
file(WRITE "${stamp}" "laying\n${RV_SDL3_SHA256}\n")

set(applied 0)
set(earlier "")
foreach(patch IN LISTS patches)
    set(path "${RV_SDL3_PATCH_DIR}/${patch}")
    execute_process(
        COMMAND "${RV_SDL3_GIT}" apply --check "${path}"
        WORKING_DIRECTORY "${RV_SDL3_SRC}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT "${rc}" STREQUAL "0")
        if(applied EQUAL 0)
            message(FATAL_ERROR "${patch} does not apply to pristine SDL ${RV_SDL3_VERSION} (SDL ${RV_SDL3_TAG}) "
                    "in ${RV_SDL3_SRC}: ${err}")
        endif()
        list(JOIN earlier ", " earlier_text)
        message(FATAL_ERROR "${patch} does not apply after ${earlier_text} of the SDL ${RV_SDL3_TAG} patch set "
                "in ${RV_SDL3_SRC}: ${err}\n"
                "The source is left partly patched; the next run restores it.")
    endif()
    execute_process(
        COMMAND "${RV_SDL3_GIT}" apply "${path}"
        WORKING_DIRECTORY "${RV_SDL3_SRC}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "cannot apply ${patch} (SDL ${RV_SDL3_TAG}) in ${RV_SDL3_SRC}: ${err}")
    endif()
    list(APPEND earlier "${patch}")
    math(EXPR applied "${applied} + 1")
endforeach()

set(copied 0)
foreach(rel IN LISTS files)
    set(from "${RV_SDL3_PATCH_DIR}/${rel}")
    set(to "${RV_SDL3_SRC}/${rel}")
    if(EXISTS "${to}")
        file(SHA256 "${from}" from_hash)
        file(SHA256 "${to}" to_hash)
        if("${from_hash}" STREQUAL "${to_hash}")
            continue()
        endif()
    endif()
    get_filename_component(to_dir "${to}" DIRECTORY)
    file(MAKE_DIRECTORY "${to_dir}")
    file(COPY_FILE "${from}" "${to}" RESULT rc)
    if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "cannot copy ${from} to ${to}: ${rc}")
    endif()
    math(EXPR copied "${copied} + 1")
endforeach()

file(WRITE "${stamp}" "${identity}")
rv_log_step("SDL ${RV_SDL3_TAG} patch set: ${applied} patches applied, ${copied} files copied")
