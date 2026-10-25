# The pinned development kit tools of the dev build (3DMPPC_DEVTOOLS on Linux, not Emscripten): Neovim, lua-language-server
# and clangd. The development kit delivery also ships the Steam Runtime 4 SDK of the internal build: disc.so is built with
# its clang, GNU ld, CMake, Ninja and readelf, so the SDK is not fetched here.
# Each download is fetched and verified by cmake/rv_fetch.cmake into the shared cache; the 3dmppc_devkit_<tool>
# target extracts it into its own dir under ${CMAKE_BINARY_DIR}/devkit/. 3dmppc_devkit_tools builds them all.
# Nothing builds them by default.
# Needs RV_FETCH_CACHE (passed by the outer build in cmake/rv_steamrt4_host.cmake, or defaulted by
# cmake/rv_steamrt4_runtime.cmake) and RV_STEAMRT4_DIR (passed by the outer build in cmake/rv_steamrt4_host.cmake;
# cmake/rv_steamrt4_runtime.cmake only checks it) and RV_STEAMRT4_SDK (cmake/rv_steamrt4_toolchain.cmake).

# One version per dependency: the URLs, file names, license sources and VERSIONS.txt lines are built from it.
set(RV_NVIM_VERSION 0.12.6)
set(RV_LUALS_VERSION 3.19.2)
set(RV_LLVM_VERSION 19.1.7)

# Neovim release v${RV_NVIM_VERSION}; SHA-256 from the release download.
set(RV_DEVKIT_NVIM_URL "https://github.com/neovim/neovim/releases/download/v${RV_NVIM_VERSION}/nvim-linux-x86_64.tar.gz")
set(RV_DEVKIT_NVIM_SHA256 "474430d53e6264f6d6dd18db42d6dc9df3a1b56ca9e88a325bbf860e1a811d87")
set(RV_DEVKIT_NVIM_FILE "nvim-${RV_NVIM_VERSION}-linux-x86_64.tar.gz")
set(RV_DEVKIT_NVIM_NAME "Neovim ${RV_NVIM_VERSION}: the code editor inside 3dmppc-editor")

# lua-language-server release ${RV_LUALS_VERSION}.
set(RV_DEVKIT_LUALS_BASE "https://github.com/LuaLS/lua-language-server/releases/download/${RV_LUALS_VERSION}/")
set(RV_DEVKIT_LUALS_URL
    "${RV_DEVKIT_LUALS_BASE}lua-language-server-${RV_LUALS_VERSION}-linux-x64.tar.gz")
set(RV_DEVKIT_LUALS_SHA256 "93ad326b10f6c00911dd8c450fa098332dbd8f9f45d1052f5636a2d54479b947")
set(RV_DEVKIT_LUALS_FILE "lua-language-server-${RV_LUALS_VERSION}-linux-x64.tar.gz")
set(RV_DEVKIT_LUALS_NAME "lua-language-server ${RV_LUALS_VERSION}: Lua diagnostics in the editor")

# LLVM release llvmorg-${RV_LLVM_VERSION}, for clangd. GitHub publishes no SHA-256 for the archive: the pin was computed
# from the download on 2026-10-25.
set(RV_DEVKIT_LLVM_BASE "https://github.com/llvm/llvm-project/releases/download/llvmorg-${RV_LLVM_VERSION}/")
set(RV_DEVKIT_LLVM_URL "${RV_DEVKIT_LLVM_BASE}LLVM-${RV_LLVM_VERSION}-Linux-X64.tar.xz")
set(RV_DEVKIT_LLVM_SHA256 "4a5ec53951a584ed36f80240f6fbf8fdd46b4cf6c7ee87cc2d5018dc37caf679")
set(RV_DEVKIT_LLVM_FILE "LLVM-${RV_LLVM_VERSION}-Linux-X64.tar.xz")
string(CONCAT RV_DEVKIT_LLVM_NAME
    "LLVM ${RV_LLVM_VERSION} release archive: the editor uses only its clangd (C and C++ diagnostics) "
    "and clang's resource dir (the built-in headers that clangd needs)")

# Extracted dirs, for the packaging that ships them.
set(RV_DEVKIT_DIR "${CMAKE_BINARY_DIR}/devkit")
set(RV_DEVKIT_NVIM_DIR "${RV_DEVKIT_DIR}/nvim")
set(RV_DEVKIT_LUALS_DIR "${RV_DEVKIT_DIR}/lua-language-server")
# The LLVM archive unpacks to LLVM-<version>-Linux-X64/ inside the llvm dir.
set(RV_DEVKIT_LLVM_DIR "${RV_DEVKIT_DIR}/llvm/LLVM-${RV_LLVM_VERSION}-Linux-X64")

# One target per tool: fetch into the shared cache, verify the pin, extract into dir when dir is set.
function(rv_devkit_fetch target url sha256 file dir name)
    add_custom_target(${target}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RV_FETCH_CACHE}"
        COMMAND ${CMAKE_COMMAND}
                -DRV_FETCH_URL=${url}
                -DRV_FETCH_SHA256=${sha256}
                "-DRV_FETCH_FILE=${RV_FETCH_CACHE}/${file}"
                "-DRV_FETCH_DIR=${dir}"
                "-DRV_FETCH_NAME=${name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_fetch.cmake"
        COMMENT "development kit download"
        VERBATIM
    )
endfunction()

rv_devkit_fetch(
    3dmppc_devkit_nvim
    "${RV_DEVKIT_NVIM_URL}"
    "${RV_DEVKIT_NVIM_SHA256}"
    "${RV_DEVKIT_NVIM_FILE}"
    "${RV_DEVKIT_NVIM_DIR}"
    "${RV_DEVKIT_NVIM_NAME}")
rv_devkit_fetch(
    3dmppc_devkit_lua_language_server
    "${RV_DEVKIT_LUALS_URL}"
    "${RV_DEVKIT_LUALS_SHA256}"
    "${RV_DEVKIT_LUALS_FILE}"
    "${RV_DEVKIT_LUALS_DIR}"
    "${RV_DEVKIT_LUALS_NAME}")
rv_devkit_fetch(
    3dmppc_devkit_llvm
    "${RV_DEVKIT_LLVM_URL}"
    "${RV_DEVKIT_LLVM_SHA256}"
    "${RV_DEVKIT_LLVM_FILE}"
    "${RV_DEVKIT_DIR}/llvm"
    "${RV_DEVKIT_LLVM_NAME}")

add_custom_target(3dmppc_devkit_tools
    DEPENDS 3dmppc_devkit_nvim 3dmppc_devkit_lua_language_server 3dmppc_devkit_llvm
)

# --- the 3dmppc development kit delivery (cmake/rv_devkit_delivery.cmake) -------
# devkit-tree and package-devkit run it on this build's outputs; neither is in ALL.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)

# The SDK the internal build compiles with; the delivery copies it. Its toolchain sets the variable.
if(NOT IS_DIRECTORY "${RV_STEAMRT4_SDK}")
    message(FATAL_ERROR "no Steam Runtime 4 SDK directory ${RV_STEAMRT4_SDK}: the delivery takes it from RV_STEAMRT4_SDK")
endif()

# Neovim's archive ships no license: it is fetched from its tag, pinned by SHA-256. Without a dir the file is only
# downloaded and verified; the delivery copies it.
set(RV_DEVKIT_NVIM_LICENSE_URL "https://raw.githubusercontent.com/neovim/neovim/v${RV_NVIM_VERSION}/LICENSE.txt")
set(RV_DEVKIT_NVIM_LICENSE_SHA256 "de23202ef9a51f5e654034190539739a4abece5505f2ce75e780791106707011")
set(RV_DEVKIT_NVIM_LICENSE_FILE "neovim-${RV_NVIM_VERSION}-LICENSE.txt")
set(RV_DEVKIT_NVIM_LICENSE_NAME
    "Neovim ${RV_NVIM_VERSION} LICENSE.txt (not in the release archive): shipped with Neovim in the development kit")
rv_devkit_fetch(
    3dmppc_devkit_license_nvim
    "${RV_DEVKIT_NVIM_LICENSE_URL}"
    "${RV_DEVKIT_NVIM_LICENSE_SHA256}"
    "${RV_DEVKIT_NVIM_LICENSE_FILE}"
    ""
    "${RV_DEVKIT_NVIM_LICENSE_NAME}")

# Each license is a NAME=PATH pair, each version a line of VERSIONS.txt.
set(RV_DEVKIT_LICENSES
    "LuaJIT.txt=${luajit_SOURCE_DIR}/COPYRIGHT"
    "ImGui.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt"
    "libvterm.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm/LICENSE"
    "cgltf.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf/LICENSE"
    "stb.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/LICENSE"
    "PxPlus-IBM-VGA.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/pxplus-ibm-vga/LICENSE"
    "win55-icons.md=${CMAKE_CURRENT_SOURCE_DIR}/third_party/win55-icons/LICENSE.md"
    "Catppuccin.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/catppuccin-palette/LICENSE"
    "LLVM.txt=${RV_DEVKIT_LLVM_DIR}/include/llvm/Support/LICENSE.TXT"
    "Neovim.txt=${RV_FETCH_CACHE}/${RV_DEVKIT_NVIM_LICENSE_FILE}"
    "lua-language-server.txt=${RV_DEVKIT_LUALS_DIR}/LICENSE")
set(RV_DEVKIT_VERSION_LINES
    "LuaJIT ${RV_LUAJIT_COMMIT}"
    "clang ${CMAKE_CXX_COMPILER_VERSION} (Steam Runtime 4 SDK)"
    "clangd ${RV_LLVM_VERSION}"
    "Neovim ${RV_NVIM_VERSION}"
    "lua-language-server ${RV_LUALS_VERSION}")

set(RV_DEVKIT_DELIVERY_TREE "${CMAKE_BINARY_DIR}/devkit-delivery/3dmppc-devkit")
set(RV_DEVKIT_DELIVERY_ZIP "${CMAKE_BINARY_DIR}/3dmppc-devkit-${RV_MPPC_VER_MAJOR}.${RV_MPPC_VER_MINOR}.zip")
set(RV_DEVKIT_DELIVERY_ARGS
    "-DRV_DEVKIT_BIN=${CMAKE_BINARY_DIR}/pconsole"
    "-DRV_DEVKIT_LAUNCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_launch.sh"
    "-DRV_DEVKIT_LLVM=${RV_DEVKIT_LLVM_DIR}"
    "-DRV_DEVKIT_NVIM=${RV_DEVKIT_NVIM_DIR}/nvim-linux-x86_64"
    "-DRV_DEVKIT_LUALS=${RV_DEVKIT_LUALS_DIR}"
    "-DRV_DEVKIT_COMPAT_CHECK=${CMAKE_BINARY_DIR}/tools/linux-compat-check.cmake"
    "-DRV_DEVKIT_TOOLCHAIN=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_steamrt4_toolchain.cmake"
    "-DRV_DEVKIT_STEAMRT4_SDK=${RV_STEAMRT4_SDK}"
    "-DRV_DEVKIT_RUNTIME=${RV_STEAMRT4_DIR}"
    "-DRV_DEVKIT_VER_MAJOR=${RV_MPPC_VER_MAJOR}"
    "-DRV_DEVKIT_VER_MINOR=${RV_MPPC_VER_MINOR}"
    "-DRV_DEVKIT_TREE=${RV_DEVKIT_DELIVERY_TREE}")
set(RV_DEVKIT_DELIVERY_DEPENDS
    3dmppc 3dmppc_editor 3dmppc_pdktools 3dmppc_firmware 3dmppc_webkit 3dmppc_devkit_tools
    3dmppc_devkit_license_nvim)

# The lists are quoted in COMMAND: an unquoted list would split the NAME=PATH pairs at their ';'.
add_custom_target(devkit-tree
    DEPENDS ${RV_DEVKIT_DELIVERY_DEPENDS}
    COMMAND ${CMAKE_COMMAND} ${RV_DEVKIT_DELIVERY_ARGS}
            "-DRV_DEVKIT_LICENSES=${RV_DEVKIT_LICENSES}"
            "-DRV_DEVKIT_VERSIONS=${RV_DEVKIT_VERSION_LINES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_devkit_delivery.cmake"
    COMMENT "Building the 3dmppc development kit delivery tree"
    VERBATIM
)
add_custom_target(package-devkit
    DEPENDS ${RV_DEVKIT_DELIVERY_DEPENDS}
    COMMAND ${CMAKE_COMMAND} ${RV_DEVKIT_DELIVERY_ARGS}
            "-DRV_DEVKIT_LICENSES=${RV_DEVKIT_LICENSES}"
            "-DRV_DEVKIT_VERSIONS=${RV_DEVKIT_VERSION_LINES}"
            "-DRV_DEVKIT_ZIP=${RV_DEVKIT_DELIVERY_ZIP}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_devkit_delivery.cmake"
    COMMENT "Packaging the 3dmppc development kit delivery"
    VERBATIM
)
