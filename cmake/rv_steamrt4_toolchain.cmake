# Toolchain for building disc.so against the Steam Runtime 4 Platform. The 3dmppc development kit delivery ships this file
# as tools/; the Steam Runtime 4 SDK sysroot archive, unpacked, is steamrt4-sdk/ beside it. Inside pressure-vessel the host
# glibc replaces the Steam Runtime 4 Platform's, so the link needs the untouched Steam Runtime 4 SDK sysroot as --sysroot.
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=<delivery>/tools/steamrt4-toolchain.cmake ...

get_filename_component(RV_STEAMRT4_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../steamrt4-sdk" ABSOLUTE)
if(NOT IS_DIRECTORY "${RV_STEAMRT4_SYSROOT}")
    message(FATAL_ERROR "no Steam Runtime 4 SDK sysroot ${RV_STEAMRT4_SYSROOT}: "
            "the 3dmppc development kit delivery must ship steamrt4-sdk beside tools/")
endif()

set(CMAKE_SYSROOT "${RV_STEAMRT4_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${RV_STEAMRT4_SYSROOT}")
# find_library, find_path and find_package search only the sysroot; find_program is not rerooted, so clang and
# clang++ come from PATH. lld is reached through -fuse-ld=lld.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# By name: the delivery puts LLVM first on PATH.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# lld, not GNU ld: the Steam Runtime 4 Platform has no ld; the Steam Runtime 4 SDK's GNU ld needs libbfd,
# which the Platform lacks.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
