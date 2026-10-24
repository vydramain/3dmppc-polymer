# Builds the 3dmppc development kit delivery tree: bin/ (a dev build's pconsole/), the launch copies beside bin/,
# tools/ (LLVM, CMake, Ninja, Neovim, lua-language-server, the compat check and toolchain), steamrt4-sdk/ (the Steam
# Runtime 4 SDK sysroot), SteamLinuxRuntime_4/, LICENSES/ and VERSIONS.txt; with a zip path, also the zip of the tree.
# No build target runs it yet. No project license is shipped: the repository has none.
#
# Run: cmake -DRV_DEVKIT_BIN=<pconsole dir> -DRV_DEVKIT_LAUNCH=<rv_launch.sh> -DRV_DEVKIT_LLVM=<LLVM top dir>
#            -DRV_DEVKIT_CMAKE=<cmake dir> -DRV_DEVKIT_NINJA=<ninja binary> -DRV_DEVKIT_NVIM=<nvim dir>
#            -DRV_DEVKIT_LUALS=<lua-language-server dir> -DRV_DEVKIT_COMPAT_CHECK=<rv_linux_compat_check.cmake>
#            -DRV_DEVKIT_TOOLCHAIN=<rv_steamrt4_toolchain.cmake> -DRV_DEVKIT_SYSROOT=<Steam Runtime 4 SDK sysroot dir>
#            -DRV_DEVKIT_RUNTIME=<SteamLinuxRuntime_4 dir> "-DRV_DEVKIT_LICENSES=NAME=PATH;..."
#            "-DRV_DEVKIT_VERSIONS=line;..." -DRV_DEVKIT_VER_MAJOR=<n> -DRV_DEVKIT_VER_MINOR=<n>
#            -DRV_DEVKIT_TREE=<dir> [-DRV_DEVKIT_ZIP=<zip>] -P <this file>

cmake_minimum_required(VERSION 3.24)

foreach(var RV_DEVKIT_BIN RV_DEVKIT_LAUNCH RV_DEVKIT_LLVM RV_DEVKIT_CMAKE RV_DEVKIT_NINJA RV_DEVKIT_NVIM RV_DEVKIT_LUALS
            RV_DEVKIT_COMPAT_CHECK RV_DEVKIT_TOOLCHAIN RV_DEVKIT_SYSROOT RV_DEVKIT_RUNTIME RV_DEVKIT_LICENSES
            RV_DEVKIT_VERSIONS RV_DEVKIT_VER_MAJOR RV_DEVKIT_VER_MINOR RV_DEVKIT_TREE)
    # A value of 0 is false in if(), so the test is STREQUAL "".
    if("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be specified via -D${var}=<value>")
    endif()
endforeach()

# The optional zip is found before the tree is built: a missing zip fails the run without a half-made package.
if(RV_DEVKIT_ZIP)
    find_program(RV_DEVKIT_ZIP_PROGRAM zip)
    if(NOT RV_DEVKIT_ZIP_PROGRAM)
        message(FATAL_ERROR "zip is not found: install zip to package ${RV_DEVKIT_ZIP}")
    endif()
endif()

get_filename_component(RV_DEVKIT_ROOT_NAME "${RV_DEVKIT_TREE}" NAME)
get_filename_component(RV_DEVKIT_PARENT "${RV_DEVKIT_TREE}" DIRECTORY)
set(RV_DEVKIT_EXEC_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# The launch file runs bin/<its own name>, so each console and tool name gets its own launch copy beside bin/.
set(RV_DEVKIT_LAUNCH_NAMES 3dmppc 3dmppc-editor mppcburner mppctexbaker mppcmusbaker mppcmodbaker)

# The LLVM binaries disc builds and the editor use, from LLVM's bin/: checked below, copied into tools/llvm/bin.
set(RV_DEVKIT_LLVM_FILES clang-23 clangd lld llvm-readobj)

# Every input is checked before the tree is removed: a bad input must not cost the last good tree.
foreach(var RV_DEVKIT_BIN RV_DEVKIT_LLVM RV_DEVKIT_CMAKE RV_DEVKIT_NVIM RV_DEVKIT_LUALS RV_DEVKIT_SYSROOT RV_DEVKIT_RUNTIME)
    if(NOT IS_DIRECTORY "${${var}}")
        message(FATAL_ERROR "${var} is not a directory: ${${var}}")
    endif()
endforeach()
foreach(var RV_DEVKIT_LAUNCH RV_DEVKIT_NINJA RV_DEVKIT_COMPAT_CHECK RV_DEVKIT_TOOLCHAIN)
    if(NOT EXISTS "${${var}}")
        message(FATAL_ERROR "${var} is not found: ${${var}}")
    endif()
endforeach()
foreach(name IN LISTS RV_DEVKIT_LAUNCH_NAMES)
    if(NOT EXISTS "${RV_DEVKIT_BIN}/${name}")
        message(FATAL_ERROR "RV_DEVKIT_BIN has no ${name}: the launch copy needs ${RV_DEVKIT_BIN}/${name}")
    endif()
endforeach()
foreach(name IN LISTS RV_DEVKIT_LLVM_FILES)
    if(NOT EXISTS "${RV_DEVKIT_LLVM}/bin/${name}")
        message(FATAL_ERROR "RV_DEVKIT_LLVM has no bin/${name}: the tree copies ${RV_DEVKIT_LLVM}/bin/${name}")
    endif()
endforeach()
foreach(dir IN ITEMS "${RV_DEVKIT_LLVM}/lib/clang" "${RV_DEVKIT_CMAKE}/bin" "${RV_DEVKIT_CMAKE}/share")
    if(NOT IS_DIRECTORY "${dir}")
        message(FATAL_ERROR "input directory is not found: ${dir}")
    endif()
endforeach()
foreach(file IN ITEMS "${RV_DEVKIT_RUNTIME}/VERSIONS.txt" "${RV_DEVKIT_SYSROOT}/usr/lib/os-release")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "version input is not found: ${file}")
    endif()
endforeach()
foreach(pair IN LISTS RV_DEVKIT_LICENSES)
    if(NOT pair MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR "license must be NAME=PATH: ${pair}")
    endif()
    if(NOT EXISTS "${CMAKE_MATCH_2}")
        message(FATAL_ERROR "license file is not found: ${CMAKE_MATCH_2}")
    endif()
endforeach()

# Rebuilt from scratch every run: a file left by an earlier tree or zip must not ship.
file(REMOVE_RECURSE "${RV_DEVKIT_TREE}")
if(RV_DEVKIT_ZIP)
    file(REMOVE "${RV_DEVKIT_ZIP}")
endif()
file(MAKE_DIRECTORY "${RV_DEVKIT_TREE}/bin" "${RV_DEVKIT_TREE}/LICENSES" "${RV_DEVKIT_TREE}/tools")

# file(COPY) keeps the symlinks and modes of the dev build's pconsole/ tree, firmware/SteamLinuxRuntime_4 included.
# Its var/ is left out: pressure-vessel writes state there, with absolute symlinks into the builder's home.
file(COPY "${RV_DEVKIT_BIN}/" DESTINATION "${RV_DEVKIT_TREE}/bin" REGEX "/SteamLinuxRuntime_4/var$" EXCLUDE)
foreach(name IN LISTS RV_DEVKIT_LAUNCH_NAMES)
    file(COPY_FILE "${RV_DEVKIT_LAUNCH}" "${RV_DEVKIT_TREE}/${name}")
    file(CHMOD "${RV_DEVKIT_TREE}/${name}" PERMISSIONS ${RV_DEVKIT_EXEC_PERMISSIONS})
endforeach()

# Only what disc builds and the editor use: the full LLVM tree is over a gigabyte. The symlink names are kept.
# The Steam Runtime 4 Platform has no binutils, so llvm-readobj stands in as llvm-readelf for the compat check.
set(RV_DEVKIT_LLVM_BIN "${RV_DEVKIT_TREE}/tools/llvm/bin")
file(MAKE_DIRECTORY "${RV_DEVKIT_LLVM_BIN}")
foreach(name IN LISTS RV_DEVKIT_LLVM_FILES)
    file(COPY_FILE "${RV_DEVKIT_LLVM}/bin/${name}" "${RV_DEVKIT_LLVM_BIN}/${name}")
endforeach()
file(CREATE_LINK clang-23 "${RV_DEVKIT_LLVM_BIN}/clang" SYMBOLIC)
file(CREATE_LINK clang "${RV_DEVKIT_LLVM_BIN}/clang++" SYMBOLIC)
file(CREATE_LINK lld "${RV_DEVKIT_LLVM_BIN}/ld.lld" SYMBOLIC)
file(CREATE_LINK llvm-readobj "${RV_DEVKIT_LLVM_BIN}/llvm-readelf" SYMBOLIC)
file(COPY "${RV_DEVKIT_LLVM}/lib/clang/" DESTINATION "${RV_DEVKIT_TREE}/tools/llvm/lib/clang")

file(COPY "${RV_DEVKIT_CMAKE}/bin" "${RV_DEVKIT_CMAKE}/share" DESTINATION "${RV_DEVKIT_TREE}/tools/cmake")
file(MAKE_DIRECTORY "${RV_DEVKIT_TREE}/tools/ninja/bin")
file(COPY_FILE "${RV_DEVKIT_NINJA}" "${RV_DEVKIT_TREE}/tools/ninja/bin/ninja")
file(CHMOD "${RV_DEVKIT_TREE}/tools/ninja/bin/ninja" PERMISSIONS ${RV_DEVKIT_EXEC_PERMISSIONS})
file(COPY "${RV_DEVKIT_NVIM}/" DESTINATION "${RV_DEVKIT_TREE}/tools/nvim")
file(COPY "${RV_DEVKIT_LUALS}/" DESTINATION "${RV_DEVKIT_TREE}/tools/lua-language-server")
file(COPY_FILE "${RV_DEVKIT_COMPAT_CHECK}" "${RV_DEVKIT_TREE}/tools/linux-compat-check.cmake")
file(COPY_FILE "${RV_DEVKIT_TOOLCHAIN}" "${RV_DEVKIT_TREE}/tools/steamrt4-toolchain.cmake")

file(COPY "${RV_DEVKIT_SYSROOT}/" DESTINATION "${RV_DEVKIT_TREE}/steamrt4-sdk")
# The regex matches the runtime's full source path, so the path is escaped without a trailing slash; its var/ is left
# out as for bin/.
string(REGEX REPLACE "/+$" "" RV_DEVKIT_RUNTIME_DIR "${RV_DEVKIT_RUNTIME}")
string(REGEX REPLACE "([][.+*?^$(){}|\\\\])" "\\\\\\1" RV_DEVKIT_RUNTIME_RE "${RV_DEVKIT_RUNTIME_DIR}")
file(COPY "${RV_DEVKIT_RUNTIME_DIR}/" DESTINATION "${RV_DEVKIT_TREE}/SteamLinuxRuntime_4"
     REGEX "^${RV_DEVKIT_RUNTIME_RE}/var$" EXCLUDE)

foreach(pair IN LISTS RV_DEVKIT_LICENSES)
    string(REGEX MATCH "^([^=]+)=(.+)$" unused "${pair}")
    file(COPY_FILE "${CMAKE_MATCH_2}" "${RV_DEVKIT_TREE}/LICENSES/${CMAKE_MATCH_1}")
endforeach()

# The Steam Linux Runtime 4 depot version, e.g. 4.0.20261021.265997, and the sysroot's BUILD_ID.
file(READ "${RV_DEVKIT_RUNTIME}/VERSIONS.txt" RV_DEVKIT_RUNTIME_VERSIONS)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" RV_DEVKIT_RUNTIME_VER "${RV_DEVKIT_RUNTIME_VERSIONS}")
if("${RV_DEVKIT_RUNTIME_VER}" STREQUAL "")
    message(FATAL_ERROR "no depot version in ${RV_DEVKIT_RUNTIME}/VERSIONS.txt")
endif()
file(STRINGS "${RV_DEVKIT_SYSROOT}/usr/lib/os-release" RV_DEVKIT_OS_RELEASE REGEX "^BUILD_ID=")
string(REGEX REPLACE "^BUILD_ID=\"?([^\"]*)\"?$" "\\1" RV_DEVKIT_SYSROOT_BUILD "${RV_DEVKIT_OS_RELEASE}")
if("${RV_DEVKIT_SYSROOT_BUILD}" STREQUAL "")
    message(FATAL_ERROR "no BUILD_ID in ${RV_DEVKIT_SYSROOT}/usr/lib/os-release")
endif()

# string(APPEND) keeps one string: set() with several arguments would make a list joined by ';'.
set(RV_DEVKIT_VERSIONS_TEXT "3dmppc ${RV_DEVKIT_VER_MAJOR}.${RV_DEVKIT_VER_MINOR}\n")
string(APPEND RV_DEVKIT_VERSIONS_TEXT "Steam Linux Runtime 4 ${RV_DEVKIT_RUNTIME_VER}\n")
string(APPEND RV_DEVKIT_VERSIONS_TEXT "Steam Runtime 4 SDK sysroot ${RV_DEVKIT_SYSROOT_BUILD}\n")
foreach(line IN LISTS RV_DEVKIT_VERSIONS)
    string(APPEND RV_DEVKIT_VERSIONS_TEXT "${line}\n")
endforeach()
file(WRITE "${RV_DEVKIT_TREE}/VERSIONS.txt" "${RV_DEVKIT_VERSIONS_TEXT}")

# Every ELF under bin/ and tools/ is checked against the tree's own Steam Runtime 4 Platform. Steam Linux Runtime 4
# dirs are skipped: the Platform is the reference the check compares against, not a binary to check.
string(REPLACE "[" "[[]" RV_DEVKIT_GLOB_TREE "${RV_DEVKIT_TREE}")
string(REPLACE "*" "[*]" RV_DEVKIT_GLOB_TREE "${RV_DEVKIT_GLOB_TREE}")
string(REPLACE "?" "[?]" RV_DEVKIT_GLOB_TREE "${RV_DEVKIT_GLOB_TREE}")
file(GLOB_RECURSE RV_DEVKIT_FILES LIST_DIRECTORIES FALSE
     "${RV_DEVKIT_GLOB_TREE}/bin/*" "${RV_DEVKIT_GLOB_TREE}/tools/*")
set(RV_DEVKIT_ELFS "")
foreach(file IN LISTS RV_DEVKIT_FILES)
    if("${file}" MATCHES "/SteamLinuxRuntime_4/")
        continue()
    endif()
    if(IS_SYMLINK "${file}")
        # GLOB_RECURSE lists a symlinked directory without descending into it: its files would go unchecked.
        if(IS_DIRECTORY "${file}")
            message(FATAL_ERROR "symlinked directory ${file} hides its files from the compat check: make it a real directory")
        endif()
        continue()
    endif()
    file(READ "${file}" magic LIMIT 4 HEX)
    if(magic STREQUAL "7f454c46")
        list(APPEND RV_DEVKIT_ELFS "${file}")
    endif()
endforeach()
if(NOT RV_DEVKIT_ELFS)
    message(FATAL_ERROR "no ELF file to check under ${RV_DEVKIT_TREE}/bin or tools")
endif()
list(LENGTH RV_DEVKIT_ELFS RV_DEVKIT_ELF_COUNT)
message(STATUS "compat check: ${RV_DEVKIT_ELF_COUNT} ELF files")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DRV_COMPAT_FILES=${RV_DEVKIT_ELFS}"
            "-DRV_COMPAT_RUNTIME=${RV_DEVKIT_TREE}/SteamLinuxRuntime_4"
            -P "${CMAKE_CURRENT_LIST_DIR}/rv_linux_compat_check.cmake"
    RESULT_VARIABLE RV_DEVKIT_COMPAT_RESULT
)
if(NOT RV_DEVKIT_COMPAT_RESULT EQUAL 0)
    message(FATAL_ERROR "the 3dmppc development kit delivery tree breaks the Linux compatibility contract; see the check above")
endif()

if(RV_DEVKIT_ZIP)
    # Run from the parent so the archive's top dir is the root's name; -y keeps the symlinks.
    execute_process(
        COMMAND "${RV_DEVKIT_ZIP_PROGRAM}" -r -y -q "${RV_DEVKIT_ZIP}" "${RV_DEVKIT_ROOT_NAME}"
        WORKING_DIRECTORY "${RV_DEVKIT_PARENT}"
        RESULT_VARIABLE RV_DEVKIT_ZIP_RESULT
    )
    if(NOT RV_DEVKIT_ZIP_RESULT EQUAL 0)
        message(FATAL_ERROR "zip failed on ${RV_DEVKIT_TREE}")
    endif()
endif()
