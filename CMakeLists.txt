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

# --- LuaJIT --------------------------------------------------------------------
# TODO: Need to make fork with CMake build system only for my project for easiest
#       import to system
find_package(LuaJIT QUIET)
if(NOT LuaJIT_FOUND)
  message(STATUS "LuaJIT not found on system - fetching from source.")
  FetchContent_Declare(
    LuaJIT
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG v2.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(LuaJIT)
endif()
add_custom_command(OUTPUT ${luajit_SOURCE_DIR}/src/libluajit.a COMMAND make WORKING_DIRECTORY ${luajit_SOURCE_DIR})
add_custom_target(libluajit_maker DEPENDS ${luajit_SOURCE_DIR}/src/libluajit.a)
add_library(libluajit STATIC IMPORTED)
set_target_properties(libluajit PROPERTIES IMPORTED_LOCATION ${luajit_SOURCE_DIR}/src/libluajit.a INTERFACE_INCLUDE_DIRECTORIES ${luajit_SOURCE_DIR}/src)
add_dependencies(libluajit libluajit_maker)

# NEUROSLOP-BEGIN (claude-opus-5)
# stb_image is gone with the demo that used it. The console never decodes an
# image format: a game uploads raw texels through rv_cv::video_asset_write, and
# decoding whatever a disc chose to store is the DISC's business (via rv_cd).

# --- phantasy development kit -------------------------------------------------

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk/include)

# --- shared helper library (pdklib) -------------------------------------------
# Header-only implementations written AGAINST the contract: matrix math, camera,
# transform to screen-space vertices, format parsers, diagnostic output. It is
# not the contract (the console implements nothing here) and not a game, so it
# is its own tree.
#
# It is INTERFACE because every function in it is inline and lives in a header:
# there is no .cpp under pdk/lib/ and therefore no artifact to build. Linking it
# delivers one thing, the include directory.
#
# Discs get it a different way — mppcburner writes the path into the CMakeLists
# it generates for a disc project — so this target serves only what is built
# from this file.
add_library(3dmppc_pdklib INTERFACE)
target_include_directories(3dmppc_pdklib INTERFACE ${CMAKE_SOURCE_DIR}/pdk/lib/include)

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
target_link_libraries(rv_dmain PRIVATE 3dmppc_pdklib)
target_link_libraries(rv_dmain PRIVATE libluajit)

# Stamps every log line this target emits with its origin, so game lines are
# told apart from console lines without anyone passing an argument. Four
# characters wide; see pdklib/rv_logs.hpp.
target_compile_definitions(rv_dmain PRIVATE RV_LOG_ORIGIN="disc")

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
target_link_libraries(3dmppc PRIVATE libluajit)
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)
# Its own line, next to the contract and not folded into it: pdklib is a
# separate tree with a separate purpose, and the console takes from it only the
# output stack — pdklib/rv_stdio.hpp and pdklib/rv_logs.hpp on top of it.
target_link_libraries(3dmppc PRIVATE 3dmppc_pdklib)
target_link_libraries(3dmppc PRIVATE rv_dmain)

target_compile_definitions(3dmppc PRIVATE RV_LOG_ORIGIN="mppc")

# NEUROSLOP-BEGIN (claude-opus-5)
# dlopen/dlsym: how a .mppcdisc's code enters the process. CMAKE_DL_LIBS is
# "-ldl" where that is a separate library and empty where it is part of libc.
target_link_libraries(3dmppc PRIVATE ${CMAKE_DL_LIBS})
# NEUROSLOP-END

if(MSVC)
  target_compile_options(3dmppc PRIVATE /W4)
else()
  target_compile_options(3dmppc PRIVATE -Wall -Wextra)
endif()

# No asset copying: nothing reads files yet. Disc assets come back when the disc
# drive (rv_cd) lands and a disc reads them by name instead of by path.

# The authoring tools are NOT built here. They live in pdk/tools/ and build with
# their own command:
#
#   cmake -S pdk/tools -B pdk/tools/build && cmake --build pdk/tools/build
#
# The console is firmware: it loads a disc and runs it. It must never look like
# the thing that compiles one, and building it must not drag in a compiler
# driver, an image decoder, or anything else a player's machine has no use for.
# NEUROSLOP-END
