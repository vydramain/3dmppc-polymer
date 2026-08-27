cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(3dmppc LANGUAGES CXX)


if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/luajit.cmake)

# NEUROSLOP-BEGIN (claude-opus-5)
# stb_image is gone with the demo that used it. The console never decodes an
# image format: a game uploads raw texels through rv_cv::video_asset_write, and
# decoding whatever a disc chose to store is the DISC's business (via rv_cd).

# --- phantasy development kit -------------------------------------------------

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk/include)

# --- shared helper library (pdklib) -------------------------------------------
# Disc-side helpers written AGAINST the contract: mostly header-only math,
# camera, transform, text, and diagnostics, plus the statically linked manifest
# parser implementation. It is not the contract (the console implements nothing
# here) and not a game, so it is its own tree.
#
# Linking it now delivers both things pdklib owns: the include directory and the
# manifest parser object code built from pdk/lib/include/pdklib/rv_manifest/.
#
# Discs get it a different way — mppcburner writes the path into the CMakeLists
# it generates for a disc project — so this target serves only what is built
# from this file.
file(GLOB_RECURSE 3DMPPC_PDKLIB_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/pdk/lib/include/pdklib/rv_manifest/*.cpp
)
add_library(3dmppc_pdklib STATIC ${3DMPPC_PDKLIB_SOURCES})
target_include_directories(3dmppc_pdklib PUBLIC ${CMAKE_SOURCE_DIR}/pdk/lib/include)
target_link_libraries(3dmppc_pdklib PUBLIC 3dmppc_pdk)

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
# characters wide; see pdklib/rv_logs/rv_logs.hpp.
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
# output stack — pdklib/rv_stdio/rv_stdio.hpp and pdklib/rv_logs/rv_logs.hpp on
# top of it.
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
