# The Linux delivery contract for a binary we ship: the player console firmware and
# a disc's disc.so. They run on a Steam Runtime 3 "sniper" machine (glibc 2.31, no
# newer libstdc++), where a binary that needs more dies before main.
#
# Run: cmake -DRV_COMPAT_FILES="<file>[;<file>...]" [-DRV_COMPAT_GLIBC_MAX=2.31] -P <this file>
# Callers: check-linux-compat (CMakeLists.txt) for the firmware; mppcburner will check disc.so with it.

if(NOT RV_COMPAT_FILES)
    message(FATAL_ERROR "RV_COMPAT_FILES must be specified via -DRV_COMPAT_FILES=<file>[;<file>...]")
endif()
if(NOT RV_COMPAT_GLIBC_MAX)
    set(RV_COMPAT_GLIBC_MAX 2.31)
endif()

# Everything a shipped binary may link dynamically.
set(RV_COMPAT_ALLOWED_LIBS libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 ld-linux-x86-64.so.2)

set(RV_COMPAT_HINT_STATIC "libstdc++ and libgcc are linked statically: build with -static-libstdc++ -static-libgcc")
set(RV_COMPAT_HINT_LIB "a library off the list: link it statically or build it into the binary")
set(RV_COMPAT_HINT_SYSROOT
    "a glibc newer than ${RV_COMPAT_GLIBC_MAX}: build against the sniper sysroot (glibc ${RV_COMPAT_GLIBC_MAX})")

find_program(RV_COMPAT_READELF readelf)
if(NOT RV_COMPAT_READELF)
    message(FATAL_ERROR "readelf not found: install binutils")
endif()

# Runs readelf -W ${flag} on one file; its stdout is left in RV_COMPAT_TEXT.
function(rv_compat_readelf file flag)
    execute_process(COMMAND ${RV_COMPAT_READELF} -W ${flag} ${file}
        OUTPUT_VARIABLE text RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "readelf ${flag} failed on ${file}: ${err}")
    endif()
    set(RV_COMPAT_TEXT "${text}" PARENT_SCOPE)
endfunction()

# Sets ${libs_var} to the NEEDED libraries of ${file} and ${vers_var} to one
# "<library>=<version>" token per version need, so each need keeps its library.
function(rv_compat_read file libs_var vers_var)
    set(libs "")
    set(vers "")
    rv_compat_readelf(${file} -d)
    string(REPLACE "\n" ";" rows "${RV_COMPAT_TEXT}")
    foreach(row IN LISTS rows)
        if(row MATCHES "\\(NEEDED\\).*\\[([^]]+)\\]")
            list(APPEND libs "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    rv_compat_readelf(${file} -V)
    string(REPLACE "\n" ";" rows "${RV_COMPAT_TEXT}")
    set(owner "")
    foreach(row IN LISTS rows)
        # A section header ends the needs: a definition names this file's own exports.
        if(row MATCHES "section")
            set(owner "")
        elseif(row MATCHES "File: ([^ ]+)")
            set(owner "${CMAKE_MATCH_1}")
        elseif(owner AND row MATCHES "Name: ([^ ]+)")
            list(APPEND vers "${owner}=${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${libs_var} "${libs}" PARENT_SCOPE)
    set(${vers_var} "${vers}" PARENT_SCOPE)
endfunction()

set(RV_COMPAT_PROBLEMS "")
set(RV_COMPAT_HINTS "")
foreach(file IN LISTS RV_COMPAT_FILES)
    if(NOT EXISTS ${file})
        message(FATAL_ERROR "no such file: ${file}")
    endif()
    rv_compat_read(${file} needed needs)

    foreach(lib IN LISTS needed)
        if(NOT lib IN_LIST RV_COMPAT_ALLOWED_LIBS)
            list(APPEND RV_COMPAT_PROBLEMS "${file}: NEEDED ${lib} is not on the allowed list")
            if(lib MATCHES "^lib(stdc\\+\\+|gcc_s)\\.so")
                list(APPEND RV_COMPAT_HINTS "${RV_COMPAT_HINT_STATIC}")
            else()
                list(APPEND RV_COMPAT_HINTS "${RV_COMPAT_HINT_LIB}")
            endif()
        endif()
    endforeach()

    set(newest 0)
    foreach(need IN LISTS needs)
        string(REPLACE "=" ";" parts "${need}")
        list(GET parts 0 lib)
        list(GET parts 1 version)
        # ld-linux's private interface: every binary linked against libc carries it.
        if(version STREQUAL "GLIBC_PRIVATE")
            continue()
        endif()
        if(NOT version MATCHES "^GLIBC_([0-9][0-9.]*)$")
            list(APPEND RV_COMPAT_PROBLEMS "${file}: ${version} (needed by ${lib}) - only GLIBC_x.y version needs are allowed")
            if(version MATCHES "^(GLIBCXX|CXXABI|GCC)_")
                list(APPEND RV_COMPAT_HINTS "${RV_COMPAT_HINT_STATIC}")
            else()
                list(APPEND RV_COMPAT_HINTS "${RV_COMPAT_HINT_LIB}")
            endif()
            continue()
        endif()
        set(glibc "${CMAKE_MATCH_1}")
        if(glibc VERSION_GREATER ${RV_COMPAT_GLIBC_MAX})
            list(APPEND RV_COMPAT_PROBLEMS "${file}: ${version} (needed by ${lib}) is newer than GLIBC_${RV_COMPAT_GLIBC_MAX}")
            list(APPEND RV_COMPAT_HINTS "${RV_COMPAT_HINT_SYSROOT}")
        endif()
        if(glibc VERSION_GREATER ${newest})
            set(newest "${glibc}")
        endif()
    endforeach()

    if(needed)
        list(JOIN needed ", " needed_text)
    else()
        set(needed_text "no libraries")
    endif()
    if(newest STREQUAL "0")
        set(newest_text "no GLIBC version")
    else()
        set(newest_text "newest GLIBC_${newest}")
    endif()
    message(STATUS "${file}: needs ${needed_text}; ${newest_text}")
endforeach()

if(RV_COMPAT_PROBLEMS)
    list(REMOVE_DUPLICATES RV_COMPAT_HINTS)
    list(JOIN RV_COMPAT_PROBLEMS "\n  " report)
    list(JOIN RV_COMPAT_HINTS "\n  " hints)
    message(FATAL_ERROR
        "Linux compatibility violations (cmake/rv_linux_compat_check.cmake):\n  ${report}\n"
        "Fix:\n  ${hints}")
endif()
