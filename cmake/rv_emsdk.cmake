include_guard(GLOBAL)

# --- the web kit ----------------------------------------------------------------
# The kit is built with a pinned emsdk, not the system's: the dev build fetches it into
# _deps like SDL3 and LuaJIT, so one build command makes the kit. Its compiler download
# runs once, at build time, not at configure.
set(RV_EMSDK_VERSION 6.0.10)
FetchContent_Declare(emsdk
  URL https://github.com/emscripten-core/emsdk/archive/refs/tags/${RV_EMSDK_VERSION}.tar.gz
  URL_HASH SHA256=09cbafdf00e5a7b4275fc27229d69befafc5ca6070543124219977101310dade
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(emsdk)

# The stamp records that the compiler is installed. It lives in the emsdk tree, so a re-fetched emsdk installs again.
set(RV_EMSDK_STAMP ${emsdk_SOURCE_DIR}/installed-${RV_EMSDK_VERSION}.stamp)
add_custom_command(OUTPUT ${RV_EMSDK_STAMP}
  COMMAND ./emsdk install ${RV_EMSDK_VERSION}
  COMMAND ./emsdk activate ${RV_EMSDK_VERSION}
  COMMAND ${CMAKE_COMMAND} -E touch ${RV_EMSDK_STAMP}
  WORKING_DIRECTORY ${emsdk_SOURCE_DIR}
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
  -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/pconsole/firmware/web
)
# A system SDL3 is native and cannot link into wasm: the kit takes the SDL3 source the dev build fetched, else fetches its own.
if(DEFINED sdl3_SOURCE_DIR)
  list(APPEND RV_WEBKIT_CMAKE_ARGS -DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl3_SOURCE_DIR})
endif()

ExternalProject_Add(3dmppc_webkit
  SOURCE_DIR ${CMAKE_SOURCE_DIR}
  BINARY_DIR ${CMAKE_BINARY_DIR}/webkit-build
  CMAKE_ARGS ${RV_WEBKIT_CMAKE_ARGS}
  BUILD_ALWAYS ON
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
  DEPENDS 3dmppc_emsdk
)
