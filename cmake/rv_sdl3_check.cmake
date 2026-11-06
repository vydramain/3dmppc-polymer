# SDL3 version check, run by the check-sdl3-version build target (cmake/sdl.cmake).
#
# The build links the SDL3 of the Steam Runtime 4 SDK sysroot, so the pinned RV_SDL3_VERSION
# must equal the version in that sysroot's SDL_version.h. Run it with:
#   cmake -DRV_SDL3_VERSION=<x.y.z> -DRV_SDL3_HEADER=<.../SDL3/SDL_version.h> -P cmake/rv_sdl3_check.cmake

if(NOT RV_SDL3_VERSION)
    message(FATAL_ERROR "RV_SDL3_VERSION must be specified via -DRV_SDL3_VERSION=<x.y.z>")
endif()
if(NOT RV_SDL3_HEADER)
    message(FATAL_ERROR "RV_SDL3_HEADER must be specified via -DRV_SDL3_HEADER=<path to SDL3/SDL_version.h>")
endif()
if(NOT EXISTS "${RV_SDL3_HEADER}")
    message(FATAL_ERROR "SDL3 version header not found: ${RV_SDL3_HEADER}")
endif()

# Sets ${out} to the number in the header's "#define ${name} <number>" line.
function(rv_sdl3_read_define name out)
    file(READ "${RV_SDL3_HEADER}" content)
    string(REGEX MATCH "#define[ \t]+${name}[ \t]+([0-9]+)" matched "${content}")
    if(NOT matched)
        message(FATAL_ERROR "${name} not found in ${RV_SDL3_HEADER}")
    endif()
    set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

rv_sdl3_read_define(SDL_MAJOR_VERSION major)
rv_sdl3_read_define(SDL_MINOR_VERSION minor)
rv_sdl3_read_define(SDL_MICRO_VERSION micro)
set(found "${major}.${minor}.${micro}")

if(NOT found STREQUAL RV_SDL3_VERSION)
    message(FATAL_ERROR
        "SDL3 version mismatch: the build's SDL3 is ${found} (${RV_SDL3_HEADER}), "
        "the pinned RV_SDL3_VERSION in cmake/sdl.cmake is ${RV_SDL3_VERSION}.\n"
        "Raise RV_SDL3_VERSION and its archive pin in cmake/sdl.cmake together with the runtime, "
        "or keep the runtime at ${RV_SDL3_VERSION}.")
endif()
message(STATUS "check-sdl3-version: ok (SDL ${found})")
