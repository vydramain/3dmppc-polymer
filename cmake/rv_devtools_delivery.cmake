# Updates the 3dmppc devtools delivery tree <tree> (<build>/pconsole/ in the build): bin/ (copied from the dev build's
# <build>/bin: console, editor, mppcburner, bakers, firmware), the launch copies beside bin/, tools/ (clangd with clang's
# resource dir: built-in headers and compiler-rt runtimes; Neovim, lua-language-server, the compat check and toolchain),
# SteamLinuxRuntime_4/, LICENSES/ and VERSIONS.txt. steamrt4-sdk/ at the root is unpacked by the outer build and only read:
# the launchers run the programs with it, so it is not copied (see the SDK check below).
# devtools-tree runs this in ALL, so each build updates the tree in place. bin/ is never cleared: the player's memcard and
# modes.toml live there. A file the build dropped stays until deleted by hand. The runtime copy is replaced only when its
# VERSIONS.txt differs. Every input runs before the first write. The compat check covers only the ELF sources of this run
# (what is copied now), not files that an earlier build left in the tree. No project license is shipped: the repository
# has none.
#
# Run: cmake -DRV_DEVTOOLS_BIN=<build>/bin -DRV_DEVTOOLS_LAUNCH=<rv_launch.sh> -DRV_DEVTOOLS_LLVM=<LLVM 19.1.7 release dir>
#            -DRV_DEVTOOLS_NVIM=<nvim dir>
#            -DRV_DEVTOOLS_LUALS=<lua-language-server dir> -DRV_DEVTOOLS_COMPAT_CHECK=<rv_linux_compat_check.cmake>
#            -DRV_DEVTOOLS_TOOLCHAIN=<rv_steamrt4_toolchain.cmake> -DRV_DEVTOOLS_STEAMRT4_SDK=<tree>/steamrt4-sdk
#            -DRV_DEVTOOLS_RUNTIME=<SteamLinuxRuntime_4 dir> "-DRV_DEVTOOLS_LICENSES=NAME=PATH;..."
#            "-DRV_DEVTOOLS_VERSIONS=line;..." -DRV_DEVTOOLS_VER_MAJOR=<n> -DRV_DEVTOOLS_VER_MINOR=<n>
#            -DRV_DEVTOOLS_TREE=<dir> -P <this file>

cmake_minimum_required(VERSION 3.24)

foreach(var RV_DEVTOOLS_BIN RV_DEVTOOLS_LAUNCH RV_DEVTOOLS_LLVM RV_DEVTOOLS_NVIM RV_DEVTOOLS_LUALS
            RV_DEVTOOLS_COMPAT_CHECK RV_DEVTOOLS_TOOLCHAIN RV_DEVTOOLS_STEAMRT4_SDK RV_DEVTOOLS_RUNTIME RV_DEVTOOLS_LICENSES
            RV_DEVTOOLS_VERSIONS RV_DEVTOOLS_VER_MAJOR RV_DEVTOOLS_VER_MINOR RV_DEVTOOLS_TREE)
    # A value of 0 is false in if(), so the test is STREQUAL "".
    if("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be specified via -D${var}=<value>")
    endif()
endforeach()

set(RV_DEVTOOLS_EXEC_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# The launch file runs bin/<its own name>, so each console and tool name gets its own launch copy beside bin/.
set(RV_DEVTOOLS_LAUNCH_NAMES 3dmppc 3dmppc-editor mppcburner mppctexbaker mppcmusbaker mppcmodbaker)

# Every input is checked before anything is written: a bad input must not cost the tree the player runs from.
foreach(var RV_DEVTOOLS_BIN RV_DEVTOOLS_LLVM RV_DEVTOOLS_NVIM RV_DEVTOOLS_LUALS RV_DEVTOOLS_STEAMRT4_SDK RV_DEVTOOLS_RUNTIME)
    if(NOT IS_DIRECTORY "${${var}}")
        message(FATAL_ERROR "${var} is not a directory: ${${var}}")
    endif()
endforeach()
foreach(var RV_DEVTOOLS_LAUNCH RV_DEVTOOLS_COMPAT_CHECK RV_DEVTOOLS_TOOLCHAIN)
    if(NOT EXISTS "${${var}}")
        message(FATAL_ERROR "${var} is not found: ${${var}}")
    endif()
endforeach()
foreach(name IN LISTS RV_DEVTOOLS_LAUNCH_NAMES)
    if(NOT EXISTS "${RV_DEVTOOLS_BIN}/${name}")
        message(FATAL_ERROR "RV_DEVTOOLS_BIN has no ${name}: the launch copy needs ${RV_DEVTOOLS_BIN}/${name}")
    endif()
endforeach()
if(NOT EXISTS "${RV_DEVTOOLS_LLVM}/bin/clangd")
    message(FATAL_ERROR "RV_DEVTOOLS_LLVM has no bin/clangd: the tree copies ${RV_DEVTOOLS_LLVM}/bin/clangd")
endif()
if(NOT IS_DIRECTORY "${RV_DEVTOOLS_LLVM}/lib/clang")
    message(FATAL_ERROR "RV_DEVTOOLS_LLVM has no lib/clang: clangd needs its resource dir from there")
endif()
foreach(file IN ITEMS "${RV_DEVTOOLS_RUNTIME}/VERSIONS.txt" "${RV_DEVTOOLS_STEAMRT4_SDK}/usr/lib/os-release")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "version input is not found: ${file}")
    endif()
endforeach()
foreach(pair IN LISTS RV_DEVTOOLS_LICENSES)
    if(NOT pair MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR "license must be NAME=PATH: ${pair}")
    endif()
    if(NOT EXISTS "${CMAKE_MATCH_2}")
        message(FATAL_ERROR "license file is not found: ${CMAKE_MATCH_2}")
    endif()
endforeach()

# The launchers run the programs with the SDK at the tree root, and the delivery does not copy it: any other path
# would leave them on a second, unchecked SDK.
get_filename_component(RV_DEVTOOLS_SDK_REAL "${RV_DEVTOOLS_STEAMRT4_SDK}" REALPATH)
get_filename_component(RV_DEVTOOLS_TREE_SDK "${RV_DEVTOOLS_TREE}/steamrt4-sdk" REALPATH)
if(NOT RV_DEVTOOLS_SDK_REAL STREQUAL RV_DEVTOOLS_TREE_SDK)
    message(FATAL_ERROR "RV_DEVTOOLS_STEAMRT4_SDK must be ${RV_DEVTOOLS_TREE}/steamrt4-sdk, not ${RV_DEVTOOLS_STEAMRT4_SDK}: "
            "the launchers run the programs with the SDK at the tree root, and the delivery does not copy it")
endif()

# The Steam Linux Runtime 4 depot version, e.g. 4.0.20261021.265997, and the SDK's BUILD_ID.
file(READ "${RV_DEVTOOLS_RUNTIME}/VERSIONS.txt" RV_DEVTOOLS_RUNTIME_VERSIONS)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" RV_DEVTOOLS_RUNTIME_VER "${RV_DEVTOOLS_RUNTIME_VERSIONS}")
if("${RV_DEVTOOLS_RUNTIME_VER}" STREQUAL "")
    message(FATAL_ERROR "no depot version in ${RV_DEVTOOLS_RUNTIME}/VERSIONS.txt")
endif()
file(STRINGS "${RV_DEVTOOLS_STEAMRT4_SDK}/usr/lib/os-release" RV_DEVTOOLS_OS_RELEASE REGEX "^BUILD_ID=")
string(REGEX REPLACE "^BUILD_ID=\"?([^\"]*)\"?$" "\\1" RV_DEVTOOLS_SDK_BUILD "${RV_DEVTOOLS_OS_RELEASE}")
if("${RV_DEVTOOLS_SDK_BUILD}" STREQUAL "")
    message(FATAL_ERROR "no BUILD_ID in ${RV_DEVTOOLS_STEAMRT4_SDK}/usr/lib/os-release")
endif()

# string(APPEND) keeps one string: set() with several arguments would make a list joined by ';'.
set(RV_DEVTOOLS_VERSIONS_TEXT "3dmppc ${RV_DEVTOOLS_VER_MAJOR}.${RV_DEVTOOLS_VER_MINOR}\n")
string(APPEND RV_DEVTOOLS_VERSIONS_TEXT "Steam Linux Runtime 4 ${RV_DEVTOOLS_RUNTIME_VER}\n")
string(APPEND RV_DEVTOOLS_VERSIONS_TEXT "Steam Runtime 4 SDK ${RV_DEVTOOLS_SDK_BUILD}\n")
foreach(line IN LISTS RV_DEVTOOLS_VERSIONS)
    string(APPEND RV_DEVTOOLS_VERSIONS_TEXT "${line}\n")
endforeach()

# Lists the ELF files under each root: a directory is searched recursively, a file is taken as it is. Steam Linux
# Runtime 4 dirs are skipped: the runtime is the reference the check compares against, not a binary to check.
function(rv_devtools_find_elfs out)
    set(candidates "")
    foreach(root IN LISTS ARGN)
        if(IS_DIRECTORY "${root}")
            # GLOB_RECURSE takes a pattern: the glob characters of the root are escaped.
            string(REPLACE "[" "[[]" glob "${root}")
            string(REPLACE "*" "[*]" glob "${glob}")
            string(REPLACE "?" "[?]" glob "${glob}")
            file(GLOB_RECURSE found LIST_DIRECTORIES FALSE "${glob}/*")
            list(APPEND candidates ${found})
        else()
            list(APPEND candidates "${root}")
        endif()
    endforeach()
    set(elfs "")
    foreach(file IN LISTS candidates)
        if("${file}" MATCHES "/SteamLinuxRuntime_4/")
            continue()
        endif()
        if(IS_SYMLINK "${file}")
            # GLOB_RECURSE lists a symlinked directory without descending into it: its files would go unchecked.
            if(IS_DIRECTORY "${file}")
                message(FATAL_ERROR "symlinked directory ${file} hides its files from the compat check: "
                        "make it a real directory")
            endif()
            continue()
        endif()
        file(READ "${file}" magic LIMIT 4 HEX)
        if(magic STREQUAL "7f454c46")
            list(APPEND elfs "${file}")
        endif()
    endforeach()
    set(${out} "${elfs}" PARENT_SCOPE)
endfunction()

# The ELF files are read from their sources, the ones the tree will copy. Each must find its libraries and symbol
# versions in the runtime it will run in, so the check runs before the tree is written.
rv_devtools_find_elfs(RV_DEVTOOLS_ELFS "${RV_DEVTOOLS_BIN}" "${RV_DEVTOOLS_LLVM}/bin/clangd" "${RV_DEVTOOLS_LLVM}/lib/clang"
                      "${RV_DEVTOOLS_NVIM}" "${RV_DEVTOOLS_LUALS}")
if(NOT RV_DEVTOOLS_ELFS)
    message(FATAL_ERROR "no ELF file to check under ${RV_DEVTOOLS_BIN} or in the tools' sources")
endif()
list(LENGTH RV_DEVTOOLS_ELFS RV_DEVTOOLS_ELF_COUNT)
message(
    STATUS
    "Checking ${RV_DEVTOOLS_ELF_COUNT} ELF files of the sources against Steam Linux Runtime 4 ${RV_DEVTOOLS_RUNTIME_VER} "
    "(libraries and symbol versions each binary needs)")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DRV_COMPAT_FILES=${RV_DEVTOOLS_ELFS}"
            "-DRV_COMPAT_RUNTIME=${RV_DEVTOOLS_RUNTIME}"
            -P "${CMAKE_CURRENT_LIST_DIR}/rv_linux_compat_check.cmake"
    RESULT_VARIABLE RV_DEVTOOLS_COMPAT_RESULT
)
if(NOT RV_DEVTOOLS_COMPAT_RESULT EQUAL 0)
    message(FATAL_ERROR "the 3dmppc devtools delivery breaks the Linux compatibility contract; see the check above. "
            "${RV_DEVTOOLS_TREE} is not changed.")
endif()

# Written in place: existing files are overwritten, and nothing is removed from bin/, tools/ or LICENSES/.
message(STATUS "Updating the devtools delivery tree in ${RV_DEVTOOLS_TREE}")
file(MAKE_DIRECTORY "${RV_DEVTOOLS_TREE}/bin" "${RV_DEVTOOLS_TREE}/LICENSES" "${RV_DEVTOOLS_TREE}/tools")

# file(COPY) keeps the symlinks and modes of the dev build's bin/, firmware/SteamLinuxRuntime_4 included.
# Its var/ is left out: pressure-vessel writes state there, with absolute symlinks into the builder's home.
message(STATUS "Copying the dev build's bin/ (console, editor, mppcburner, bakers, firmware) to ${RV_DEVTOOLS_TREE}/bin")
file(COPY "${RV_DEVTOOLS_BIN}/" DESTINATION "${RV_DEVTOOLS_TREE}/bin" REGEX "/SteamLinuxRuntime_4/var$" EXCLUDE)
message(STATUS "Adding the launch scripts beside bin/: each runs bin/<its own name>")
foreach(name IN LISTS RV_DEVTOOLS_LAUNCH_NAMES)
    file(COPY_FILE "${RV_DEVTOOLS_LAUNCH}" "${RV_DEVTOOLS_TREE}/${name}")
    file(CHMOD "${RV_DEVTOOLS_TREE}/${name}" PERMISSIONS ${RV_DEVTOOLS_EXEC_PERMISSIONS})
endforeach()

# Only clangd and clang's resource dir (built-in headers, compiler-rt runtimes): the full LLVM tree is over a gigabyte.
# No compiler is shipped: disc builds use the clang of the Steam Runtime 4 SDK.
set(RV_DEVTOOLS_LLVM_BIN "${RV_DEVTOOLS_TREE}/tools/llvm/bin")
message(STATUS "Copying clangd and its resource dir to ${RV_DEVTOOLS_TREE}/tools/llvm")
file(MAKE_DIRECTORY "${RV_DEVTOOLS_LLVM_BIN}")
file(COPY_FILE "${RV_DEVTOOLS_LLVM}/bin/clangd" "${RV_DEVTOOLS_LLVM_BIN}/clangd")
file(COPY "${RV_DEVTOOLS_LLVM}/lib/clang/" DESTINATION "${RV_DEVTOOLS_TREE}/tools/llvm/lib/clang")

message(STATUS "Copying Neovim, lua-language-server, the compat check and the toolchain to ${RV_DEVTOOLS_TREE}/tools")
file(COPY "${RV_DEVTOOLS_NVIM}/" DESTINATION "${RV_DEVTOOLS_TREE}/tools/nvim")
file(COPY "${RV_DEVTOOLS_LUALS}/" DESTINATION "${RV_DEVTOOLS_TREE}/tools/lua-language-server")
file(COPY_FILE "${RV_DEVTOOLS_COMPAT_CHECK}" "${RV_DEVTOOLS_TREE}/tools/linux-compat-check.cmake")
file(COPY_FILE "${RV_DEVTOOLS_TOOLCHAIN}" "${RV_DEVTOOLS_TREE}/tools/steamrt4-toolchain.cmake")

# The runtime copy is current when its VERSIONS.txt equals the source's; a missing or different copy is replaced whole.
set(RV_DEVTOOLS_RUNTIME_COPY "${RV_DEVTOOLS_TREE}/SteamLinuxRuntime_4")
set(RV_DEVTOOLS_RUNTIME_COPY_VERSIONS "")
if(EXISTS "${RV_DEVTOOLS_RUNTIME_COPY}/VERSIONS.txt")
    file(READ "${RV_DEVTOOLS_RUNTIME_COPY}/VERSIONS.txt" RV_DEVTOOLS_RUNTIME_COPY_VERSIONS)
endif()
if("${RV_DEVTOOLS_RUNTIME_COPY_VERSIONS}" STREQUAL "${RV_DEVTOOLS_RUNTIME_VERSIONS}")
    message(STATUS "Steam Linux Runtime 4 ${RV_DEVTOOLS_RUNTIME_VER} is already in ${RV_DEVTOOLS_RUNTIME_COPY}: not copied")
else()
    message(STATUS "Copying Steam Linux Runtime 4 ${RV_DEVTOOLS_RUNTIME_VER} to ${RV_DEVTOOLS_RUNTIME_COPY}: "
                   "its pressure-vessel starts the bin/ programs; the compat check uses it as its platform")
    # The copy is built beside the old one and renamed into place only when complete, so an interrupted copy
    # never leaves a partial tree that carries the current stamp.
    set(RV_DEVTOOLS_RUNTIME_PART "${RV_DEVTOOLS_RUNTIME_COPY}.part")
    file(REMOVE_RECURSE "${RV_DEVTOOLS_RUNTIME_PART}")
    # The regex matches the runtime's full source path, so the path is escaped without a trailing slash; its var/ is
    # left out as for bin/.
    string(REGEX REPLACE "/+$" "" RV_DEVTOOLS_RUNTIME_DIR "${RV_DEVTOOLS_RUNTIME}")
    string(REGEX REPLACE "([][.+*?^$(){}|\\\\])" "\\\\\\1" RV_DEVTOOLS_RUNTIME_RE "${RV_DEVTOOLS_RUNTIME_DIR}")
    file(COPY "${RV_DEVTOOLS_RUNTIME_DIR}/" DESTINATION "${RV_DEVTOOLS_RUNTIME_PART}"
         REGEX "^${RV_DEVTOOLS_RUNTIME_RE}/var$" EXCLUDE)
    # The old copy goes only after the new one is complete.
    file(REMOVE_RECURSE "${RV_DEVTOOLS_RUNTIME_COPY}")
    file(RENAME "${RV_DEVTOOLS_RUNTIME_PART}" "${RV_DEVTOOLS_RUNTIME_COPY}")
endif()

message(STATUS "Copying the licenses to ${RV_DEVTOOLS_TREE}/LICENSES")
foreach(pair IN LISTS RV_DEVTOOLS_LICENSES)
    string(REGEX MATCH "^([^=]+)=(.+)$" unused "${pair}")
    file(COPY_FILE "${CMAKE_MATCH_2}" "${RV_DEVTOOLS_TREE}/LICENSES/${CMAKE_MATCH_1}")
endforeach()

message(STATUS "Writing ${RV_DEVTOOLS_TREE}/VERSIONS.txt with the project, runtime and SDK versions")
file(WRITE "${RV_DEVTOOLS_TREE}/VERSIONS.txt" "${RV_DEVTOOLS_VERSIONS_TEXT}")
