# The Linux delivery contract for a binary we ship: the player console firmware, the dev console and the tools, and a
# disc's disc.so. They run under Steam Linux Runtime 4, whose Platform libraries are the reference: a binary that needs
# a library, or a symbol version of one, the Platform does not define dies before main.
#
# Run: cmake -DRV_COMPAT_FILES="<file>[;<file>...]" -DRV_COMPAT_RUNTIME=<SteamLinuxRuntime_4 dir> -P <this file>
# Callers: check-linux-compat (CMakeLists.txt); mppcburner runs its copy in tools/ to check disc.so.

cmake_minimum_required(VERSION 3.24)

if(NOT RV_COMPAT_FILES)
    message(FATAL_ERROR "RV_COMPAT_FILES must be specified via -DRV_COMPAT_FILES=<file>[;<file>...]")
endif()
if(NOT RV_COMPAT_RUNTIME)
    message(FATAL_ERROR "RV_COMPAT_RUNTIME must be specified via -DRV_COMPAT_RUNTIME=<SteamLinuxRuntime_4 dir>")
endif()

find_program(RV_COMPAT_READELF NAMES readelf llvm-readelf)
if(NOT RV_COMPAT_READELF)
    message(FATAL_ERROR "readelf not found: install binutils (readelf) or LLVM (llvm-readelf)")
endif()

# Sets ${var} to the paths in ${dir} matching ${name}. The directory is a literal path, but file(GLOB) reads
# [ * ? in it as patterns, so they are escaped as bracket classes first; a bare ] is literal.
function(rv_compat_glob var dir name)
    string(REPLACE "[" "[[]" dir "${dir}")
    string(REPLACE "*" "[*]" dir "${dir}")
    string(REPLACE "?" "[?]" dir "${dir}")
    file(GLOB found "${dir}/${name}")
    set(${var} "${found}" PARENT_SCOPE)
endfunction()

rv_compat_glob(RV_COMPAT_PLATFORMS "${RV_COMPAT_RUNTIME}" "steamrt4_platform_*")
list(LENGTH RV_COMPAT_PLATFORMS RV_COMPAT_PLATFORM_COUNT)
if(NOT RV_COMPAT_PLATFORM_COUNT EQUAL 1)
    message(FATAL_ERROR "expected one steamrt4_platform_* dir in ${RV_COMPAT_RUNTIME}, found ${RV_COMPAT_PLATFORM_COUNT}: "
            "${RV_COMPAT_PLATFORMS}")
endif()
set(RV_COMPAT_PLATFORM_LIBDIR "${RV_COMPAT_PLATFORMS}/files/lib/x86_64-linux-gnu")
if(NOT IS_DIRECTORY "${RV_COMPAT_PLATFORM_LIBDIR}")
    message(FATAL_ERROR "no ${RV_COMPAT_PLATFORM_LIBDIR}: ${RV_COMPAT_PLATFORMS} is not a Steam Runtime 4 Platform")
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

# The Platform's files do not always carry their SONAME in their name (libjsoncpp.so.1.9.6 has SONAME
# libjsoncpp.so.26), so the NEEDED names are looked up by the SONAME each regular ELF file carries.
# Sets RV_COMPAT_SONAME_<soname> in the caller's scope to the file; a later file with the same SONAME wins.
function(rv_compat_map_platform)
    rv_compat_glob(entries "${RV_COMPAT_PLATFORM_LIBDIR}" "*")
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${entry}" OR IS_SYMLINK "${entry}")
            continue()
        endif()
        execute_process(COMMAND ${RV_COMPAT_READELF} -W -d "${entry}"
            OUTPUT_VARIABLE text RESULT_VARIABLE rc ERROR_QUIET)
        if(NOT rc EQUAL 0)
            # Not an ELF file: it cannot provide a NEEDED library.
            continue()
        endif()
        string(REPLACE "\n" ";" rows "${text}")
        foreach(row IN LISTS rows)
            if(row MATCHES "\\(SONAME\\).*\\[([^]]+)\\]")
                set("RV_COMPAT_SONAME_${CMAKE_MATCH_1}" "${entry}" PARENT_SCOPE)
            endif()
        endforeach()
    endforeach()
endfunction()

# Sets ${var} to the version names ${file} defines: the entries of its "Version definition section".
function(rv_compat_definitions file var)
    rv_compat_readelf(${file} -V)
    string(REPLACE "\n" ";" rows "${RV_COMPAT_TEXT}")
    set(defs "")
    set(in_defs FALSE)
    foreach(row IN LISTS rows)
        if(row MATCHES "section")
            set(in_defs FALSE)
            if(row MATCHES "Version definition section")
                set(in_defs TRUE)
            endif()
        elseif(in_defs AND row MATCHES "Name: ([^ ]+)")
            list(APPEND defs "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${var} "${defs}" PARENT_SCOPE)
endfunction()

rv_compat_map_platform()

set(RV_COMPAT_PROBLEMS "")
foreach(file IN LISTS RV_COMPAT_FILES)
    if(NOT EXISTS ${file})
        message(FATAL_ERROR "no such file: ${file}")
    endif()
    rv_compat_read(${file} needed needs)
    list(LENGTH RV_COMPAT_PROBLEMS problems_before)

    # Each library is looked up and its definitions read once; RV_COMPAT_DEFS_<library> caches them.
    foreach(lib IN LISTS needed)
        if(NOT DEFINED "RV_COMPAT_DEFS_${lib}")
            if(NOT DEFINED "RV_COMPAT_SONAME_${lib}")
                list(APPEND RV_COMPAT_PROBLEMS "${file}: NEEDED ${lib} is not in Steam Runtime 4 Platform")
                continue()
            endif()
            rv_compat_definitions("${RV_COMPAT_SONAME_${lib}}" defs)
            set("RV_COMPAT_DEFS_${lib}" "${defs}")
        endif()
    endforeach()

    foreach(need IN LISTS needs)
        string(REPLACE "=" ";" parts "${need}")
        list(GET parts 0 lib)
        list(GET parts 1 version)
        # ld-linux's private interface: every binary linked against libc carries it.
        if(version STREQUAL "GLIBC_PRIVATE")
            continue()
        endif()
        # A library the Platform lacks is reported above; there is nothing to compare its versions with.
        if(NOT DEFINED "RV_COMPAT_DEFS_${lib}")
            continue()
        endif()
        set(defs "${RV_COMPAT_DEFS_${lib}}")
        if(NOT version IN_LIST defs)
            list(APPEND RV_COMPAT_PROBLEMS
                 "${file}: ${version} (needed from ${lib}) is not defined by Steam Runtime 4 Platform's ${lib}")
        endif()
    endforeach()

    list(LENGTH RV_COMPAT_PROBLEMS problems_after)
    if(needed)
        list(JOIN needed ", " needed_text)
    else()
        set(needed_text "no libraries")
    endif()
    if(problems_after EQUAL problems_before)
        message(STATUS "${file}: needs ${needed_text}; all versions defined by the platform")
    else()
        message(STATUS "${file}: needs ${needed_text}; see the problems below")
    endif()
endforeach()

if(RV_COMPAT_PROBLEMS)
    list(JOIN RV_COMPAT_PROBLEMS "\n  " report)
    message(FATAL_ERROR "Linux compatibility violations (cmake/rv_linux_compat_check.cmake):\n  ${report}")
endif()
