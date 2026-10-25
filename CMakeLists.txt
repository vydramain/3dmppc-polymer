cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_TOOLCHAIN_FILE)
  project(3dmppc LANGUAGES NONE)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_steamrt4_host.cmake)
  return()
endif()
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

# LuaJIT is the script slot (cl=luajit) and the PDK ffi.cdef bridge. OFF builds a
# console without Lua: a disc that asks for luajit with script memory > 0 is refused
# by name at load; with none, rv_pccl_resolve maps it to null and it loads.
option(3DMPPC_LUAJIT "Build the LuaJIT script slot and the PDK ffi.cdef bridge" ON)

# The disc is linked into the console instead of dlopen-ed from a .mppcdisc (the web
# build cannot dlopen). rv_pcloader_static.cpp replaces the dl half of rv_pcloader.
option(3DMPPC_STATIC_DISC "Link the disc into the console statically: no dlopen, no ELF check" OFF)
set(3DMPPC_STATIC_DISC_LIBRARY "" CACHE FILEPATH "Static library of the disc, linked into the console when 3DMPPC_STATIC_DISC is ON")
if(3DMPPC_STATIC_DISC)
  # The devtools directory route runs the ELF check, which a static disc does not have.
  if(3DMPPC_DEVTOOLS)
    message(FATAL_ERROR "3DMPPC_STATIC_DISC=ON needs 3DMPPC_DEVTOOLS=OFF")
  endif()
  # A web kit leaves the disc to mppcburner, which links it into the page.
  if(NOT 3DMPPC_WEB_KIT AND NOT 3DMPPC_STATIC_DISC_LIBRARY)
    message(FATAL_ERROR "3DMPPC_STATIC_DISC=ON needs 3DMPPC_STATIC_DISC_LIBRARY set to the disc's static library")
  endif()
endif()

# The web kit is the browser firmware that mppcburner links a disc into: the console
# as static libraries plus kit.txt, with no executable. See cmake/rv_webkit.cmake.
option(3DMPPC_WEB_KIT "Build the web kit: the console as static libraries and kit.txt for mppcburner, no executable" OFF)
if(3DMPPC_WEB_KIT)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "3DMPPC_WEB_KIT=ON needs an Emscripten build (emcmake)")
  endif()
  if(3DMPPC_DEVTOOLS OR 3DMPPC_LUAJIT OR NOT 3DMPPC_STATIC_DISC)
    message(FATAL_ERROR "3DMPPC_WEB_KIT=ON needs 3DMPPC_DEVTOOLS=OFF, 3DMPPC_LUAJIT=OFF and 3DMPPC_STATIC_DISC=ON")
  endif()
endif()

include(FetchContent)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl.cmake)
if(3DMPPC_LUAJIT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/luajit.cmake)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_cdef_gen.cmake)
# The Steam Linux Runtime 4 of the Linux delivery (its check and its packaging).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_steamrt4_runtime.cmake)
endif()

# stb_image is gone with the demo that used it. The console never decodes an
# image format: a game uploads raw texels through rv_cv::video_asset_write, and
# decoding whatever a disc chose to store is the DISC's business (via rv_cd).

# The 512-line rule is enforced by the check-length build target, not at configure time.
# Run it with: cmake --build <build-dir> --target check-length
# See cmake/rv_file_length.cmake for the algorithm.
add_custom_target(check-length
    COMMAND ${CMAKE_COMMAND}
            "-DRV_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_file_length.cmake"
    COMMENT "Checking C++ source file lengths"
    VERBATIM
)

# --- the contract and pdklib --------------------------------------------------
# 3dmppc_pdk and 3dmppc_pdklib come from one module the tools and the editor
# include too; see cmake/pdklib.cmake for what each one is. Discs get pdklib a
# different way - mppcburner writes its path into the CMakeLists it generates
# for a disc project - so these targets serve only what is built from here.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pdklib.cmake)

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
# The loader's dl half (dl, inspect, staging) or its static half, never both.
if(3DMPPC_STATIC_DISC)
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pcloader_(dl|inspect|staging)\\.cpp")
else()
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pcloader_static\\.cpp")
endif()
# The positional disc's mount()/mount_dir() choice and the medium it hands
# the drive - see src/rv_pboot/rv_pboot_discmedium.hpp for why it is two
# functions rather than a filesystem check duplicated in rv_pboot.cpp and
# rv_pboot_budget.cpp.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_discmedium_(devtools|standard)\\.cpp")
# The command channel's own option: its getopt row and its help paragraph exist
# only where the channel does - see src/rv_pboot/rv_pboot_args_cmd.hpp.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_args_cmd_(devtools|standard)\\.cpp")
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
# The embedded screen: --frame-fd exists only in a development build.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/platform/rv_pcframe_(devtools|standard)\\.cpp")
# The frame driver: native sleeps between frames, a browser calls the frame from requestAnimationFrame and must not sleep.
if(EMSCRIPTEN)
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pconsole_drive\\.cpp")
else()
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pconsole_drive_web\\.cpp")
endif()
# The available RAM: /proc/meminfo natively, the wasm heap limit in a browser.
if(EMSCRIPTEN)
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_ram_posix\\.cpp")
else()
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pboot/rv_pboot_ram_web\\.cpp")
endif()
# The entry point: a browser has no command line, so main_web.cpp passes a fixed one.
if(EMSCRIPTEN)
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
else()
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/main_web\\.cpp$")
endif()
# The cl slot's pair: LuaJIT or the null half. Only one is compiled; without
# LuaJIT none of its cl/rv_pccl_luajit* sources are.
list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/rv_pcslots_cl_(luajit|null)\\.cpp")
if(3DMPPC_LUAJIT)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pcslots_cl_luajit.cpp)
else()
  list(FILTER 3DMPPC_CONSOLE_SOURCES EXCLUDE REGEX "/src/rv_pconsole/cl/rv_pccl_luajit")
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pcslots_cl_null.cpp)
endif()
if(3DMPPC_DEVTOOLS)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pcloader_livedir_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_discmedium_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_args_cmd_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_state_devtools.cpp)
  if(3DMPPC_LUAJIT)
    list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_reload_devtools.cpp)
    list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_patch_devtools.cpp)
    list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_patchwalk_devtools.cpp)
  endif()
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdchan_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdhex_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cd/rv_pccd_fs_reload_devtools.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pcframe_devtools.cpp)
else()
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_discmedium_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pboot/rv_pboot_args_cmd_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/rv_pconsole_cmd_standard.cpp)
  if(3DMPPC_LUAJIT)
    list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cl/rv_pccl_luajit_reload_standard.cpp)
  endif()
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pccmdchan_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/cd/rv_pccd_fs_reload_standard.cpp)
  list(APPEND 3DMPPC_CONSOLE_SOURCES ${CMAKE_SOURCE_DIR}/src/rv_pconsole/platform/rv_pcframe_standard.cpp)
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
set(3DMPPC_PDK_CDEF_SOURCES "")
if(3DMPPC_LUAJIT)
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
  set(3DMPPC_PDK_CDEF_SOURCES ${RV_PDK_CDEF_OUTPUT})
endif()

# generated: not part of the glob above on purpose, so it can never leak
# into rv_dmain or 3dmppc_pdklib - only the console links against LuaJIT's
# ffi that way.
if(3DMPPC_WEB_KIT)
  # A library: the kit's disc is linked by mppcburner, so there is no link step here.
  add_library(3dmppc STATIC
    # console (game-agnostic)
    ${3DMPPC_CONSOLE_SOURCES}
    # generated: see above
    ${3DMPPC_PDK_CDEF_SOURCES}
  )
else()
  add_executable(3dmppc
    # console (game-agnostic)
    ${3DMPPC_CONSOLE_SOURCES}
    # generated: see above
    ${3DMPPC_PDK_CDEF_SOURCES}
  )
endif()

# A disc looks the contract's functions up in the executable through dlsym, and
# the LuaJIT FFI will look for them in the same place. Without ENABLE_EXPORTS
# (which is -rdynamic) the executable's dynamic symbol table is empty and neither
# side finds anything.
#
# The binary lands in build/pconsole/, next to its modes.toml and its default
# memory card.
if(NOT 3DMPPC_WEB_KIT)
  set_target_properties(3dmppc PROPERTIES
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pconsole)
endif()

target_include_directories(3dmppc PRIVATE src)
target_link_libraries(3dmppc PRIVATE 3dmppc_platform_sdl3)
if(3DMPPC_LUAJIT)
  target_link_libraries(3dmppc PRIVATE libluajit)
endif()
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)
# Its own line, next to the contract and not folded into it: pdklib is a
# separate tree with a separate purpose, and the console takes from it only the
# output stack - pdklib/rv_stdio/rv_stdio.hpp and pdklib/rv_logs/rv_logs.hpp on
# top of it.
target_link_libraries(3dmppc PRIVATE 3dmppc_pdklib)
if(NOT 3DMPPC_WEB_KIT)
  target_link_libraries(3dmppc PRIVATE rv_dmain)
endif()

target_compile_definitions(3dmppc PRIVATE RV_LOG_ORIGIN="mppc")

# dlopen/dlsym: how a .mppcdisc's code enters the process. libluajit.a also
# references dlopen/dlsym/dlerror/dlclose (ffi.load) but does not link libdl itself,
# so this line supplies them. CMAKE_DL_LIBS is "-ldl" where that is a separate
# library and empty where it is part of libc.
target_link_libraries(3dmppc PRIVATE ${CMAKE_DL_LIBS})
# With a static disc the disc is linked in as well; nothing is dlopen-ed for it.
if(3DMPPC_STATIC_DISC AND NOT 3DMPPC_WEB_KIT)
  target_link_libraries(3dmppc PRIVATE ${3DMPPC_STATIC_DISC_LIBRARY})
endif()

# A browser build keeps /save on IndexedDB (IDBFS). The pre-js mounts it and loads it before main() runs.
# The shell starts the console from a user gesture by calling Module.callMain, so it must be exported.
set(RV_WEB_LINK_FLAGS -lidbfs.js -sEXPORTED_RUNTIME_METHODS=callMain)
set(RV_WEB_PRE_JS ${CMAKE_CURRENT_SOURCE_DIR}/src/web/rv_web_pre.js)
if(EMSCRIPTEN AND NOT 3DMPPC_WEB_KIT)
  target_link_options(3dmppc PRIVATE ${RV_WEB_LINK_FLAGS} --pre-js ${RV_WEB_PRE_JS})
endif()

if(MSVC)
  target_compile_options(3dmppc PRIVATE /W4)
else()
  target_compile_options(3dmppc PRIVATE -Wall -Wextra)
endif()

# No asset copying: nothing reads files yet. Disc assets come back when the disc
# drive (rv_cd) lands and a disc reads them by name instead of by path.

# --- the development kit ------------------------------------------------------
# A player console builds alone. It is firmware: it loads a disc and runs it,
# and building it must not drag in a compiler driver, an image decoder, an
# editor, or anything else a player's machine has no use for.
#
# A development console is a developer's machine by definition, so it also
# builds the authoring tools (pdk/tools/) and the editor (editor/). Each stays
# its own CMake project, configured in its own subdirectory of this build tree
# and linking nothing from here; only their binaries land next to the console
# in pconsole/. Both still build on their own with their own command.
if(3DMPPC_DEVTOOLS)
  include(ExternalProject)
  # The pinned development kit tools (fetched by 3dmppc_devkit_tools); Linux only, as Steam Linux Runtime 4 is.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_devkit_tools.cmake)
  endif()

  # One toolchain for everything the kit builds: the whole build runs in the Steam Runtime 4 SDK
  # (steamrt4.sh) with clang from LLVM 23.1.4. The dev console exports its symbols to the disc, so
  # console and disc must share one compiler and one libstdc++.
  set(RV_KIT_CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
  # The nested builds reuse the SDL3 and LuaJIT sources fetched here instead of downloading them again.
  if(DEFINED sdl3_SOURCE_DIR)
    list(APPEND RV_KIT_CMAKE_ARGS -DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl3_SOURCE_DIR})
  endif()
  if(DEFINED luajit_SOURCE_DIR)
    list(APPEND RV_KIT_CMAKE_ARGS -DFETCHCONTENT_SOURCE_DIR_LUAJIT=${luajit_SOURCE_DIR})
  endif()

  # BUILD_ALWAYS: this build cannot see their sources, so it asks their own
  # build every time and lets it answer "no work to do".
  ExternalProject_Add(3dmppc_pdktools
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/pdk/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/pdktools
    CMAKE_ARGS ${RV_KIT_CMAKE_ARGS} -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/pconsole
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
  )
  ExternalProject_Add(3dmppc_editor
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/editor
    BINARY_DIR ${CMAKE_BINARY_DIR}/editor
    CMAKE_ARGS ${RV_KIT_CMAKE_ARGS} -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/pconsole
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
  )

  # The player console is the firmware that goes into the burned discs; the dev
  # console is not it. DEVTOOLS is one option per build tree, so the firmware
  # is a nested build with it OFF.
  ExternalProject_Add(3dmppc_firmware
    SOURCE_DIR ${CMAKE_SOURCE_DIR}
    BINARY_DIR ${CMAKE_BINARY_DIR}/firmware-build
    CMAKE_ARGS ${RV_KIT_CMAKE_ARGS}
      -D3DMPPC_DEVTOOLS=OFF
      -D3DMPPC_LUAJIT=${3DMPPC_LUAJIT}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
  )
  # The player delivery tree (player-tree) exists only where the player delivery does: Linux, LuaJIT, not
  # Emscripten, not static disc. There pconsole/firmware/ is that tree (launch file 3dmppc, bin/3dmppc, the
  # runtime, LICENSES/, VERSIONS.txt); the web kit is in pconsole/firmware/web. Elsewhere it is the bare
  # ELF at pconsole/firmware/3dmppc.
  set(RV_FIRMWARE_TREE OFF)
  set(RV_FIRMWARE_ELF ${CMAKE_BINARY_DIR}/pconsole/firmware/3dmppc)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND 3DMPPC_LUAJIT AND NOT 3DMPPC_STATIC_DISC)
    set(RV_FIRMWARE_TREE ON)
    set(RV_FIRMWARE_ELF ${CMAKE_BINARY_DIR}/pconsole/firmware/bin/3dmppc)
  endif()
  if(RV_FIRMWARE_TREE)
    # A step after the nested default build, so that build stays as it is.
    ExternalProject_Add_Step(3dmppc_firmware player_tree
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/firmware-build --target player-tree
      DEPENDEES build
      ALWAYS 1
    )
    # cp -a, not copy_directory: copy_directory dereferences the runtime's symlinks. Files of an older
    # tree stay in place; web/ is not touched.
    ExternalProject_Add_Step(3dmppc_firmware copy_firmware
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/pconsole/firmware
      COMMAND cp -a --remove-destination ${CMAKE_BINARY_DIR}/firmware-build/player/3dmppc-player/.
              ${CMAKE_BINARY_DIR}/pconsole/firmware/
      DEPENDEES player_tree
      ALWAYS 1
    )
  else()
    ExternalProject_Add_Step(3dmppc_firmware copy_firmware
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/pconsole/firmware
      COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_BINARY_DIR}/firmware-build/pconsole/3dmppc
              ${RV_FIRMWARE_ELF}
      DEPENDEES build
      ALWAYS 1
    )
  endif()

  # mppcburner, in <build>/pconsole, checks disc.so with the copy in <build>/tools/ (see cmake/rv_steamrt4_runtime.cmake).
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_linux_compat_check.cmake
                 ${CMAKE_BINARY_DIR}/tools/linux-compat-check.cmake COPYONLY)

  # The delivery contract for the shipped Linux binaries (see cmake/rv_linux_compat_check.cmake): each one
  # must need only what the Steam Runtime 4 Platform of the delivery defines. It checks the last built files,
  # so it does not rebuild them; it depends on the runtime only.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    set(RV_COMPAT_PRODUCTS
        ${CMAKE_BINARY_DIR}/pconsole/3dmppc
        ${CMAKE_BINARY_DIR}/pconsole/3dmppc-editor
        ${CMAKE_BINARY_DIR}/pconsole/mppcburner
        ${CMAKE_BINARY_DIR}/pconsole/mppctexbaker
        ${CMAKE_BINARY_DIR}/pconsole/mppcmusbaker
        ${CMAKE_BINARY_DIR}/pconsole/mppcmodbaker
        ${RV_FIRMWARE_ELF})
    add_custom_target(check-linux-compat
      DEPENDS 3dmppc_steamrt4_runtime
      COMMAND ${CMAKE_COMMAND}
              "-DRV_COMPAT_FILES=${RV_COMPAT_PRODUCTS}"
              "-DRV_COMPAT_RUNTIME=${RV_STEAMRT4_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_linux_compat_check.cmake"
      COMMENT "Checking Linux compatibility of the shipped binaries against Steam Runtime 4"
      VERBATIM
    )
    # mppcburner checks disc.so against this runtime; the player delivery's packaging will depend on it too.
    add_dependencies(3dmppc_pdktools 3dmppc_steamrt4_runtime)
  endif()

  # The web kit, built with a pinned emsdk; see cmake/rv_emsdk.cmake.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_emsdk.cmake)
endif()

# --- the web kit --------------------------------------------------------------
if(3DMPPC_WEB_KIT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_webkit.cmake)
endif()

# --- the player delivery ------------------------------------------------------
# The player delivery: the player console as a tree and a zip with Steam Linux Runtime 4 (cmake/rv_player_delivery.cmake).
if(NOT 3DMPPC_DEVTOOLS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT 3DMPPC_STATIC_DISC AND 3DMPPC_LUAJIT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)

  set(RV_PLAYER_TREE ${CMAKE_BINARY_DIR}/player/3dmppc-player)
  set(RV_PLAYER_ZIP ${CMAKE_BINARY_DIR}/3dmppc-player-${RV_MPPC_VER_MAJOR}.${RV_MPPC_VER_MINOR}.zip)
  set(RV_PLAYER_ARGS
      "-DRV_PLAYER_ELF=$<TARGET_FILE:3dmppc>"
      "-DRV_PLAYER_LAUNCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_launch.sh"
      "-DRV_PLAYER_RUNTIME=${RV_STEAMRT4_DIR}"
      "-DRV_PLAYER_LUAJIT_COPYRIGHT=${luajit_SOURCE_DIR}/COPYRIGHT"
      "-DRV_PLAYER_LUAJIT_COMMIT=${RV_LUAJIT_COMMIT}"
      "-DRV_PLAYER_VER_MAJOR=${RV_MPPC_VER_MAJOR}"
      "-DRV_PLAYER_VER_MINOR=${RV_MPPC_VER_MINOR}"
      "-DRV_PLAYER_TREE=${RV_PLAYER_TREE}")

  add_custom_target(player-tree
    DEPENDS 3dmppc 3dmppc_steamrt4_runtime
    COMMAND ${CMAKE_COMMAND} ${RV_PLAYER_ARGS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_player_delivery.cmake"
    COMMENT "Building the player delivery tree"
    VERBATIM
  )
  add_custom_target(package-player
    DEPENDS 3dmppc 3dmppc_steamrt4_runtime
    COMMAND ${CMAKE_COMMAND} ${RV_PLAYER_ARGS}
            "-DRV_PLAYER_ZIP=${RV_PLAYER_ZIP}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_player_delivery.cmake"
    COMMENT "Packaging the player delivery"
    VERBATIM
  )
endif()
if(NOT 3DMPPC_DEVTOOLS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT 3DMPPC_STATIC_DISC AND NOT 3DMPPC_LUAJIT)
  message(STATUS "no player-tree or package-player: the player delivery needs 3DMPPC_LUAJIT=ON (its LICENSES/ and VERSIONS.txt name LuaJIT)")
endif()

