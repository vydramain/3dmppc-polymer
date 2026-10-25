include_guard(GLOBAL)

# --- the web kit ----------------------------------------------------------------
# The kit is built with a pinned emsdk, not the system's: the dev build fetches it into
# _deps like SDL3 and LuaJIT, so one build command makes the kit. Its compiler download
# runs once, at build time, not at configure.
set(RV_EMSDK_VERSION 6.0.10)
# Archive of the pinned emsdk tag; URL_HASH below checks its content.
set(RV_EMSDK_URL https://github.com/emscripten-core/emsdk/archive/refs/tags/${RV_EMSDK_VERSION}.tar.gz)
FetchContent_Declare(emsdk
  URL ${RV_EMSDK_URL}
  URL_HASH SHA256=09cbafdf00e5a7b4275fc27229d69befafc5ca6070543124219977101310dade
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
# emsdk provides the Emscripten compiler that builds the web kit.
rv_log_relpath(RV_EMSDK_SRC_REL "${FETCHCONTENT_BASE_DIR}/emsdk-src" "${RV_LOG_BASE}")
rv_log_step("emsdk ${RV_EMSDK_VERSION} (web kit compiler) is fetched to ${RV_EMSDK_SRC_REL} unless an earlier configure did")
FetchContent_MakeAvailable(emsdk)

# The stamp records that the compiler is installed. It lives in the emsdk tree, so a re-fetched emsdk installs again.
set(RV_EMSDK_STAMP ${emsdk_SOURCE_DIR}/installed-${RV_EMSDK_VERSION}.stamp)
# install downloads clang/LLVM for wasm, Binaryen, Emscripten and Node.js; activate makes it current. Both run in
# one shell command, so their output is one log. emsdk's advice to add its tools to PATH does not apply: the
# build runs emsdk from its own directory.
set(RV_EMSDK_LOG ${RV_LOGS_DIR}/emsdk.log)
# The command is a list (sh, -c, script); $<SEMICOLON> keeps its ';' from splitting the -D value.
set(RV_EMSDK_CMD sh -c "./emsdk install ${RV_EMSDK_VERSION} && ./emsdk activate ${RV_EMSDK_VERSION}")
string(REPLACE ";" "$<SEMICOLON>" RV_EMSDK_CMD_ARG "${RV_EMSDK_CMD}")
add_custom_command(OUTPUT ${RV_EMSDK_STAMP}
  COMMAND ${CMAKE_COMMAND}
          "-DRV_LOG_CMD=${RV_EMSDK_CMD_ARG}"
          -DRV_LOG_FILE=${RV_EMSDK_LOG}
          -DRV_LOG_BASE=${RV_LOG_BASE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_log.cmake
  COMMAND ${CMAKE_COMMAND} -E touch ${RV_EMSDK_STAMP}
  WORKING_DIRECTORY ${emsdk_SOURCE_DIR}
  COMMENT "Installing Emscripten ${RV_EMSDK_VERSION} with emsdk (once)"
  VERBATIM
)
add_custom_target(3dmppc_emsdk DEPENDS ${RV_EMSDK_STAMP})

# The same build as the hand-run emcmake command: the emsdk toolchain file is what emcmake
# passes. Own build tree; the install lands in the firmware dir next to the console.
set(RV_WEBKIT_CMAKE_ARGS
  -DCMAKE_TOOLCHAIN_FILE=${emsdk_SOURCE_DIR}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
  -DCMAKE_BUILD_TYPE=Release
  -D3DMPPC_DEVTOOLS=OFF
  -D3DMPPC_LUAJIT=OFF
  -D3DMPPC_STATIC_DISC=ON
  -D3DMPPC_WEB_KIT=ON
  -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/bin/firmware/web
)
# A system SDL3 is native and cannot link into wasm: the kit takes the SDL3 source the dev build fetched, else fetches its own.
if(DEFINED sdl3_SOURCE_DIR)
  list(APPEND RV_WEBKIT_CMAKE_ARGS -DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl3_SOURCE_DIR})
endif()

rv_log_relpath(RV_WEBKIT_BUILD_REL "${CMAKE_BINARY_DIR}/webkit-build" "${RV_LOG_BASE}")
rv_log_relpath(RV_WEBKIT_INSTALL_REL "${CMAKE_BINARY_DIR}/bin/firmware/web" "${RV_LOG_BASE}")
rv_log_step("web kit: nested build in ${RV_WEBKIT_BUILD_REL} at build time, installed to ${RV_WEBKIT_INSTALL_REL}")
ExternalProject_Add(3dmppc_webkit
  SOURCE_DIR ${CMAKE_SOURCE_DIR}
  BINARY_DIR ${CMAKE_BINARY_DIR}/webkit-build
  CMAKE_ARGS ${RV_WEBKIT_CMAKE_ARGS}
  BUILD_ALWAYS ON
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
  DEPENDS 3dmppc_emsdk
  LOG_DIR ${RV_LOGS_DIR}
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  LOG_MERGED_STDOUTERR ON
  LOG_OUTPUT_ON_FAILURE ON
)
ExternalProject_Add_Step(3dmppc_webkit rv_announce DEPENDEES configure DEPENDERS build
  COMMAND ${CMAKE_COMMAND} -E true
  COMMENT "Nested build webkit (web kit, Emscripten): log ${RV_LOGS_REL}/3dmppc_webkit-*.log"
  ALWAYS 1
)
