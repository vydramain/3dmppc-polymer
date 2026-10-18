# Linux x86-64 toolchain: host clang against a prepared Steam Runtime sniper sysroot, so binaries run on glibc 2.31.
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/rv_linux_toolchain.cmake -DRV_LINUX_SYSROOT=<prepared sysroot> ...
# The sysroot is prepared by cmake/rv_linux_sysroot_prepare.cmake. CMake reads this file several times
# (try_compile), so every assignment must be harmless to repeat.

if(NOT RV_LINUX_SYSROOT)
    message(FATAL_ERROR "RV_LINUX_SYSROOT is not set: pass -DRV_LINUX_SYSROOT=<prepared Steam Runtime sniper sysroot>")
endif()
if(NOT IS_DIRECTORY "${RV_LINUX_SYSROOT}")
    message(FATAL_ERROR "RV_LINUX_SYSROOT is not a directory: ${RV_LINUX_SYSROOT}")
endif()
get_filename_component(RV_LINUX_SYSROOT "${RV_LINUX_SYSROOT}" REALPATH)
get_filename_component(RV_LINUX_SYSROOT_PARENT "${RV_LINUX_SYSROOT}" DIRECTORY)
set(RV_LINUX_SYSROOT_STAMP "${RV_LINUX_SYSROOT_PARENT}/sysroot-prepared.stamp")
if(NOT EXISTS "${RV_LINUX_SYSROOT_STAMP}")
    message(FATAL_ERROR "sysroot is not prepared (no ${RV_LINUX_SYSROOT_STAMP}): run "
        "cmake -DRV_SYSROOT=${RV_LINUX_SYSROOT} -P ${CMAKE_CURRENT_LIST_DIR}/rv_linux_sysroot_prepare.cmake first")
endif()

# Host clang: the sysroot's clang is 11, too old for C++23.
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
set(CMAKE_SYSROOT "${RV_LINUX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${RV_LINUX_SYSROOT}")
# Programs come from the host: cmake would otherwise pick the sysroot's clang 11.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The sysroot's default GCC is 10, which has no std::format; gcc-14 provides the C++23 headers and libstdc++.
set(RV_LINUX_GCC_INSTALL "${RV_LINUX_SYSROOT}/usr/lib/gcc-14/lib/gcc/x86_64-linux-gnu/14")
if(NOT IS_DIRECTORY "${RV_LINUX_GCC_INSTALL}")
    message(FATAL_ERROR "no gcc-14 install dir in the sysroot (--gcc-install-dir needs it): ${RV_LINUX_GCC_INSTALL}")
endif()
set(RV_LINUX_GCC_FLAGS "--gcc-install-dir=${RV_LINUX_GCC_INSTALL}")
set(CMAKE_C_FLAGS_INIT "${RV_LINUX_GCC_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${RV_LINUX_GCC_FLAGS}")

# gcc-14's libstdc++ needs GLIBCXX newer than the target systems have; linking it in keeps NEEDED to glibc only.
set(RV_LINUX_STATIC_RUNTIME "-static-libstdc++ -static-libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${RV_LINUX_STATIC_RUNTIME}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${RV_LINUX_STATIC_RUNTIME}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${RV_LINUX_STATIC_RUNTIME}")

# pkg-config reads only the sysroot's .pc files; the host's PKG_CONFIG_PATH would leak host libraries in.
# ENV is process-wide: the toolchain is for the target configure only, a host configure must not load it.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${RV_LINUX_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${RV_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${RV_LINUX_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

# SDL3 generates protocol code with this scanner; it must match the sysroot's libwayland 1.18.
# FORCE: the sysroot scanner is a requirement, not a default; a stale or host scanner generates wrong code.
set(RV_LINUX_WAYLAND_SCANNER "${RV_LINUX_SYSROOT_PARENT}/wayland-scanner")
if(NOT EXISTS "${RV_LINUX_WAYLAND_SCANNER}")
    message(FATAL_ERROR "no sysroot wayland-scanner wrapper ${RV_LINUX_WAYLAND_SCANNER}: prepare the sysroot first")
endif()
set(WAYLAND_SCANNER "${RV_LINUX_WAYLAND_SCANNER}"
    CACHE FILEPATH "wayland-scanner matching the sysroot libwayland" FORCE)

# Nested try_compile projects need the sysroot path to find the same libraries.
if(NOT "RV_LINUX_SYSROOT" IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RV_LINUX_SYSROOT)
endif()
