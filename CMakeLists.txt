cmake_minimum_required(VERSION 3.24)
project(3dmppc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- SDL3 ---------------------------------------------------------------------
# Use a system install if present, otherwise fetch and build it.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found on system — fetching from source.")
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# NEUROSLOP-BEGIN (claude-opus-5)
# stb_image is gone with the demo that used it. The console never decodes an
# image format: a game uploads raw texels through rv_cv::video_asset_write, and
# decoding whatever a disc chose to store is the DISC's business (via rv_cd).

# --- phantasy development kit -------------------------------------------------

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk/include)

# --- disc-side SDK ------------------------------------------------------------
# Header-only conveniences written AGAINST the contract, for discs only: matrix
# math, camera, transform to screen-space vertices, format parsers. It is not
# the contract (the console implements nothing here) and not a game, so it is
# its own tree — and the console below deliberately does NOT link it. If the
# console ever needs something from sdk/, that something was misfiled.
add_library(3dmppc_sdk INTERFACE)
target_include_directories(3dmppc_sdk INTERFACE ${CMAKE_SOURCE_DIR}/sdk/include)

# --- the disc -----------------------------------------------------------------
# rv_dmain is a GAME, not part of the machine, so it is built as its own target
# that links 3dmppc_pdk and NOTHING else. That is what makes "a game depends on
# PDK only" (pdk/README.md) checkable by the build instead of by discipline: the
# console's include path is not on this target, so the first #include of a
# console header here fails to compile.
#
# It is still statically linked into the executable for now — the .mppcdisc /
# dlopen packaging is stage 5 (docs/platform/disc-loading.md).
file(GLOB_RECURSE 3DMPPC_DISC_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/rv_dmain/*.cpp)

add_library(rv_dmain STATIC ${3DMPPC_DISC_SOURCES})
target_link_libraries(rv_dmain PRIVATE 3dmppc_pdk)
target_link_libraries(rv_dmain PRIVATE 3dmppc_sdk)

if(MSVC)
  target_compile_options(rv_dmain PRIVATE /W4)
else()
  target_compile_options(rv_dmain PRIVATE -Wall -Wextra)
endif()

# --- the console --------------------------------------------------------------
# CONFIGURE_DEPENDS: the build system re-checks the glob before each build, so
# a newly added .cpp is picked up without re-running cmake by hand.
file(GLOB_RECURSE 3DMPPC_CONSOLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_dmain/")

add_executable(3dmppc
  # console (game-agnostic)
  ${3DMPPC_CONSOLE_SOURCES}
)

target_include_directories(3dmppc PRIVATE src)
target_link_libraries(3dmppc PRIVATE SDL3::SDL3)
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)
target_link_libraries(3dmppc PRIVATE rv_dmain)

if(MSVC)
  target_compile_options(3dmppc PRIVATE /W4)
else()
  target_compile_options(3dmppc PRIVATE -Wall -Wextra)
endif()

# No asset copying: nothing reads files yet. Disc assets come back when the disc
# drive (rv_cd) lands and a disc reads them by name instead of by path.

# --- authoring tools ----------------------------------------------------------
# Offline converters that prepare disc content (PNG -> paletted texels, ...).
# They are BUILD-TIME tools, not part of the machine: the console never decodes
# an image format, it uploads texels that were prepared here. Their dependencies
# (stb_image) are therefore the tools' own and never reach the console.
option(3DMPPC_BUILD_TOOLS "Build the offline authoring tools" ON)
if(3DMPPC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
# NEUROSLOP-END
