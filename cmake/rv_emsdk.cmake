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
message(STATUS
  "emsdk ${RV_EMSDK_VERSION} (installs the web kit compiler) is downloaded from ${RV_EMSDK_URL} to "
  "${FETCHCONTENT_BASE_DIR}/emsdk-src, unless the previous configure already downloaded it")
FetchContent_MakeAvailable(emsdk)

# The stamp records that the compiler is installed. It lives in the emsdk tree, so a re-fetched emsdk installs again.
set(RV_EMSDK_STAMP ${emsdk_SOURCE_DIR}/installed-${RV_EMSDK_VERSION}.stamp)
string(CONCAT RV_EMSDK_INSTALL_NOTE
  "Installing Emscripten ${RV_EMSDK_VERSION} via emsdk: "
  "downloads its clang/LLVM for wasm, Binaryen, Emscripten and Node.js; "
  "activate makes it current; once, the stamp skips it afterwards; "
  "emsdk's advice to add its tools to PATH (emsdk_env.sh) does not apply, the build runs emsdk from its own directory"
)
add_custom_command(OUTPUT ${RV_EMSDK_STAMP}
  COMMAND ./emsdk install ${RV_EMSDK_VERSION}
  COMMAND ./emsdk activate ${RV_EMSDK_VERSION}
  COMMAND ${CMAKE_COMMAND} -E touch ${RV_EMSDK_STAMP}
  WORKING_DIRECTORY ${emsdk_SOURCE_DIR}
  COMMENT "${RV_EMSDK_INSTALL_NOTE}"
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

message(STATUS
  "web kit: built by a nested build in "
  "${CMAKE_BINARY_DIR}/webkit-build at build time, installed to "
  "${CMAKE_BINARY_DIR}/pconsole/firmware/web")
ExternalProject_Add(3dmppc_webkit
  SOURCE_DIR ${CMAKE_SOURCE_DIR}
  BINARY_DIR ${CMAKE_BINARY_DIR}/webkit-build
  CMAKE_ARGS ${RV_WEBKIT_CMAKE_ARGS}
  BUILD_ALWAYS ON
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
  DEPENDS 3dmppc_emsdk
)
