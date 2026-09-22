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

# The development runtime (the dev command channel, booting a game from a
# loose directory, ...) is a build-time choice, not a runtime flag: with this
# OFF the dev sources are not in the compile line at all. Two configurations
# exist - OFF is the ordinary player console; ON costs nothing a player needs.
option(3DMPPC_DEVTOOLS "Build the development runtime: the dev command channel and booting a game from a loose directory" OFF)

include(FetchContent)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/luajit.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_cdef_gen.cmake)

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
# Discs get it a different way - mppcburner writes the path into the CMakeLists
# it generates for a disc project - so this target serves only what is built
# from this file.
# The 512-line rule, checked at configure time rather than trusted. See
# cmake/rv_file_length.cmake for why the build is the right place for it.
include(cmake/rv_file_length.cmake)
rv_check_file_lengths()

file(GLOB_RECURSE 3DMPPC_PDKLIB_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/pdk/lib/include/pdklib/*.cpp
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
# rv_dmain is the console's built-in disc and is linked into the executable
# statically; external .mppcdisc packages come in through rv_pcloader instead
# (src/rv_pconsole/rv_pcloader.cpp).
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
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/platform/sdl3/")

# Dev-capability slot: mount_dir() and its helpers have no null half any more
# - a player build does not declare mount_dir() at all (rv_pcloader.hpp), so
# there is nothing here for it to define - only the dev source is excluded
# from the glob and added back by the option below.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pcloader_livedir_devtools\\.cpp")
# The positional disc's mount()/mount_dir() choice and the medium it hands
# the drive - see src/rv_pboot/rv_pboot_discmedium.hpp for why it is two
# functions rather than a filesystem check duplicated in rv_pboot.cpp and
# rv_pboot_budget.cpp.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_discmedium_(devtools|standard)\\.cpp")
# --dev itself: the getopt row and the help paragraph, which exist only where
# the development runtime does - see src/rv_pboot/rv_pboot_args_dev.hpp.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_args_dev_(devtools|standard)\\.cpp")
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pconsole_cmd_(devtools|standard)\\.cpp")
# `get`/`keys` have no null half: only the dev dispatcher calls them.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pconsole_cmd_state_devtools\\.cpp")
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/cl/rv_pccl_luajit_reload_(devtools|standard)\\.cpp")
# The in-place patch has no null half: only the dev reload calls it.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/cl/rv_pccl_luajit_patch(walk)?_devtools\\.cpp")
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/platform/rv_pccmdchan_(devtools|standard)\\.cpp")
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/cd/rv_pccd_fs_reload_(devtools|standard)\\.cpp")
# The protocol's hex encoder has no null half: nothing in a player build calls
# it, so that build simply does not compile it. Its declaration stays visible -
# a header nobody calls costs nothing - but the encoder itself is dev code.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/platform/rv_pccmdhex_devtools\\.cpp")
if(3DMPPC_DEVTOOLS)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pcloader_livedir_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_discmedium_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_args_dev_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_state_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_reload_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_patch_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_patchwalk_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdchan_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdhex_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cd/rv_pccd_fs_reload_devtools.cpp)
else()
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_discmedium_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_args_dev_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_reload_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdchan_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cd/rv_pccd_fs_reload_standard.cpp)
endif()


# --- the SDL3 platform ----------------------------------------------------------
# The only code that may see SDL. It is its own library with SDL linked PRIVATE,
# so SDL's include path never reaches the console: an #include <SDL3/SDL.h>
# anywhere outside src/rv_pconsole/platform/sdl3/ fails to compile instead of
# being caught in review. The console reaches it only through rv_pcplatform
# (src/rv_pconsole/platform/rv_pcplatform.hpp).
file(GLOB_RECURSE 3DMPPC_PLATFORM_SDL3_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/sdl3/*.cpp
)
add_library(3dmppc_platform_sdl3 STATIC ${3DMPPC_PLATFORM_SDL3_SOURCES})
target_include_directories(3dmppc_platform_sdl3 PRIVATE src)
target_link_libraries(3dmppc_platform_sdl3 PRIVATE SDL3::SDL3 3dmppc_pdk 3dmppc_pdklib)
target_compile_definitions(3dmppc_platform_sdl3 PRIVATE RV_LOG_ORIGIN="mppc")

if(MSVC)
  target_compile_options(3dmppc_platform_sdl3 PRIVATE /W4)
else()
  target_compile_options(3dmppc_platform_sdl3 PRIVATE -Wall -Wextra)
endif()

# --- PDK -> LuaJIT ffi.cdef bridge ---------------------------------------------
# ffi.cdef() takes a STRING of C declarations - it cannot follow #include and it
# cannot expand #define - so the PDK's contract is carried into the executable
# as two generated string literals (rv_pdk_cdef, rv_pdk_consts). See
# cmake/rv_cdef_gen.cmake for how, and for the single place the header
# concatenation order (RV_CDEF_ORDERED_HEADERS) lives.
#
# CONFIGURE_DEPENDS here (not just on the ordered list): rv_pdk_consts scrapes
# #define constants from every PDK header, not only the ones that carry a cdef
# region, so any header change must be able to invalidate the generated file.
file(GLOB_RECURSE RV_PDK_ALL_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/pdk/include/pdk/*.h)

set(RV_PDK_CDEF_OUTPUT ${CMAKE_BINARY_DIR}/rv_pdk_cdef.cpp)

add_custom_command(
  OUTPUT ${RV_PDK_CDEF_OUTPUT}
  COMMAND ${CMAKE_COMMAND}
          -DRV_PDK_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/pdk/include
          -DRV_CDEF_OUTPUT=${RV_PDK_CDEF_OUTPUT}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_cdef_gen.cmake
  DEPENDS ${RV_PDK_ALL_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_cdef_gen.cmake
  COMMENT "Generating LuaJIT ffi.cdef declarations from the PDK headers"
  VERBATIM
)

add_executable(3dmppc
  # console (game-agnostic)
  ${3DMPPC_CONSOLE_SOURCES}
  # generated: not part of the glob above on purpose, so it can never leak
  # into rv_dmain or 3dmppc_pdklib - only the console links against LuaJIT's
  # ffi that way.
  ${RV_PDK_CDEF_OUTPUT}
)

# A disc looks the contract's functions up in the executable through dlsym, and
# the LuaJIT FFI will look for them in the same place. Without ENABLE_EXPORTS
# (which is -rdynamic) the executable's dynamic symbol table is empty and neither
# side finds anything.
#
# The binary lands in build/pconsole/, next to its modes.toml and its default
# memory card.
set_target_properties(3dmppc PROPERTIES
  ENABLE_EXPORTS ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pconsole)

target_include_directories(3dmppc PRIVATE src)
target_link_libraries(3dmppc PRIVATE 3dmppc_platform_sdl3)
target_link_libraries(3dmppc PRIVATE libluajit)
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)
# Its own line, next to the contract and not folded into it: pdklib is a
# separate tree with a separate purpose, and the console takes from it only the
# output stack - pdklib/rv_stdio/rv_stdio.hpp and pdklib/rv_logs/rv_logs.hpp on
# top of it.
target_link_libraries(3dmppc PRIVATE 3dmppc_pdklib)
target_link_libraries(3dmppc PRIVATE rv_dmain)

target_compile_definitions(3dmppc PRIVATE RV_LOG_ORIGIN="mppc")

# dlopen/dlsym: how a .mppcdisc's code enters the process. CMAKE_DL_LIBS is
# "-ldl" where that is a separate library and empty where it is part of libc.
target_link_libraries(3dmppc PRIVATE ${CMAKE_DL_LIBS})

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

