# Toolchain for the Steam Runtime 4 Platform as target, with the Steam Runtime 4 SDK directory as sysroot.
# RV_STEAMRT4_SDK is the unpacked Steam Runtime 4 SDK. The internal project build receives it as -DRV_STEAMRT4_SDK=<dir>.
# The devtools delivery ships this file as tools/steamrt4-toolchain.cmake with steamrt4-sdk beside tools/, which
# is the default when RV_STEAMRT4_SDK is not given or empty. The SDK directory is the sysroot, so the headers and libraries
# a build links against come from the SDK whatever the host has.
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/rv_steamrt4_toolchain.cmake [-DRV_STEAMRT4_SDK=<dir>] ...

if("${RV_STEAMRT4_SDK}" STREQUAL "")
    set(RV_STEAMRT4_SDK "${CMAKE_CURRENT_LIST_DIR}/../steamrt4-sdk")
endif()
get_filename_component(RV_STEAMRT4_SDK "${RV_STEAMRT4_SDK}" ABSOLUTE)
if(NOT IS_DIRECTORY "${RV_STEAMRT4_SDK}")
    message(FATAL_ERROR "no Steam Runtime 4 SDK directory ${RV_STEAMRT4_SDK}: pass -DRV_STEAMRT4_SDK=<unpacked SDK>, "
            "or ship steamrt4-sdk beside tools/ in a devtools delivery")
endif()

# try_compile projects re-read this file and need the same directory.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RV_STEAMRT4_SDK)

set(CMAKE_SYSROOT "${RV_STEAMRT4_SDK}")
set(CMAKE_FIND_ROOT_PATH "${RV_STEAMRT4_SDK}")
# find_library, find_path and find_package search only the sysroot; find_program is not rerooted, so clang and
# clang++ come from PATH.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# By name: the SDK's clang and clang++ live in its /usr/bin, which is on PATH inside the runtime.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# No -fuse-ld: the SDK has no lld, so the link uses its default GNU ld.
