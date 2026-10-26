# The pinned devtools of the dev build (3DMPPC_DEVTOOLS on Linux, not Emscripten): Neovim, lua-language-server
# and clangd. The devtools delivery also ships the Steam Runtime 4 SDK of the internal build: disc.so is built with
# its clang, GNU ld, CMake, Ninja and readelf, so the SDK is not fetched here.
# Each download is fetched and verified by cmake/rv_fetch.cmake into the shared cache; the 3dmppc_devtools_<tool>
# target extracts it into its own dir under ${CMAKE_BINARY_DIR}/devtools/. 3dmppc_devtools_tools builds them all.
# Nothing builds them by default.
# Needs RV_FETCH_CACHE (passed by the outer build in cmake/rv_steamrt4_host.cmake, or defaulted by
# cmake/rv_steamrt4_runtime.cmake) and RV_STEAMRT4_DIR (passed by the outer build in cmake/rv_steamrt4_host.cmake;
# cmake/rv_steamrt4_runtime.cmake only checks it) and RV_STEAMRT4_SDK (cmake/rv_steamrt4_toolchain.cmake).

# One version per dependency: the URLs, file names, license sources and VERSIONS.txt lines are built from it.
set(RV_NVIM_VERSION 0.12.6)
set(RV_LUALS_VERSION 3.19.2)
set(RV_LLVM_VERSION 19.1.7)

# Neovim release v${RV_NVIM_VERSION}; SHA-256 from the release download.
set(RV_DEVTOOLS_NVIM_URL "https://github.com/neovim/neovim/releases/download/v${RV_NVIM_VERSION}/nvim-linux-x86_64.tar.gz")
set(RV_DEVTOOLS_NVIM_SHA256 "474430d53e6264f6d6dd18db42d6dc9df3a1b56ca9e88a325bbf860e1a811d87")
set(RV_DEVTOOLS_NVIM_FILE "nvim-${RV_NVIM_VERSION}-linux-x86_64.tar.gz")
# The names are the download headers in the log. Neovim is the code editor inside 3dmppc-editor.
set(RV_DEVTOOLS_NVIM_NAME "Neovim ${RV_NVIM_VERSION}")

# lua-language-server release ${RV_LUALS_VERSION}.
set(RV_DEVTOOLS_LUALS_BASE "https://github.com/LuaLS/lua-language-server/releases/download/${RV_LUALS_VERSION}/")
set(RV_DEVTOOLS_LUALS_URL
    "${RV_DEVTOOLS_LUALS_BASE}lua-language-server-${RV_LUALS_VERSION}-linux-x64.tar.gz")
set(RV_DEVTOOLS_LUALS_SHA256 "93ad326b10f6c00911dd8c450fa098332dbd8f9f45d1052f5636a2d54479b947")
set(RV_DEVTOOLS_LUALS_FILE "lua-language-server-${RV_LUALS_VERSION}-linux-x64.tar.gz")
# Lua diagnostics in the editor.
set(RV_DEVTOOLS_LUALS_NAME "lua-language-server ${RV_LUALS_VERSION}")

# LLVM release llvmorg-${RV_LLVM_VERSION}, for clangd. GitHub publishes no SHA-256 for the archive: the pin was computed
# from the download on 2026-10-25.
set(RV_DEVTOOLS_LLVM_BASE "https://github.com/llvm/llvm-project/releases/download/llvmorg-${RV_LLVM_VERSION}/")
set(RV_DEVTOOLS_LLVM_URL "${RV_DEVTOOLS_LLVM_BASE}LLVM-${RV_LLVM_VERSION}-Linux-X64.tar.xz")
set(RV_DEVTOOLS_LLVM_SHA256 "4a5ec53951a584ed36f80240f6fbf8fdd46b4cf6c7ee87cc2d5018dc37caf679")
set(RV_DEVTOOLS_LLVM_FILE "LLVM-${RV_LLVM_VERSION}-Linux-X64.tar.xz")
# Only clangd is used (C and C++ diagnostics), with clang's resource dir (the built-in headers clangd needs).
set(RV_DEVTOOLS_LLVM_NAME "LLVM ${RV_LLVM_VERSION} (clangd)")

# Extracted dirs, for the packaging that ships them.
set(RV_DEVTOOLS_DIR "${CMAKE_BINARY_DIR}/devtools")
set(RV_DEVTOOLS_NVIM_DIR "${RV_DEVTOOLS_DIR}/nvim")
set(RV_DEVTOOLS_LUALS_DIR "${RV_DEVTOOLS_DIR}/lua-language-server")
# The LLVM archive unpacks to LLVM-<version>-Linux-X64/ inside the llvm dir.
set(RV_DEVTOOLS_LLVM_DIR "${RV_DEVTOOLS_DIR}/llvm/LLVM-${RV_LLVM_VERSION}-Linux-X64")

# One target per tool: fetch into the shared cache, verify the pin, extract into dir when dir is set.
# The optional paths (a list) extract only those archive paths; empty extracts the whole archive.
function(rv_devtools_fetch target url sha256 file dir name)
    set(fetch_paths "${ARGV6}")
    # Log paths are shown relative to the user's build dir; without RV_LOG_BASE, the build dir itself.
    if(RV_LOG_BASE)
        set(fetch_base "${RV_LOG_BASE}")
    else()
        set(fetch_base "${CMAKE_BINARY_DIR}")
    endif()
    add_custom_target(${target}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RV_FETCH_CACHE}"
        COMMAND ${CMAKE_COMMAND}
                -DRV_FETCH_URL=${url}
                -DRV_FETCH_SHA256=${sha256}
                "-DRV_FETCH_FILE=${RV_FETCH_CACHE}/${file}"
                "-DRV_FETCH_DIR=${dir}"
                "-DRV_FETCH_PATHS=${fetch_paths}"
                "-DRV_FETCH_NAME=${name}"
                "-DRV_FETCH_BASE=${fetch_base}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_fetch.cmake"
        COMMENT "Fetching ${target}"
        VERBATIM
    )
endfunction()

rv_devtools_fetch(
    3dmppc_devtools_nvim
    "${RV_DEVTOOLS_NVIM_URL}"
    "${RV_DEVTOOLS_NVIM_SHA256}"
    "${RV_DEVTOOLS_NVIM_FILE}"
    "${RV_DEVTOOLS_NVIM_DIR}"
    "${RV_DEVTOOLS_NVIM_NAME}")
rv_devtools_fetch(
    3dmppc_devtools_lua_language_server
    "${RV_DEVTOOLS_LUALS_URL}"
    "${RV_DEVTOOLS_LUALS_SHA256}"
    "${RV_DEVTOOLS_LUALS_FILE}"
    "${RV_DEVTOOLS_LUALS_DIR}"
    "${RV_DEVTOOLS_LUALS_NAME}")
# The archive is about 8 GB unpacked: only what the delivery takes (clangd, clang's resource dir, the LLVM license).
set(RV_DEVTOOLS_LLVM_PATHS
    "LLVM-${RV_LLVM_VERSION}-Linux-X64/bin/clangd"
    "LLVM-${RV_LLVM_VERSION}-Linux-X64/lib/clang/"
    "LLVM-${RV_LLVM_VERSION}-Linux-X64/include/llvm/Support/LICENSE.TXT")
rv_devtools_fetch(
    3dmppc_devtools_llvm
    "${RV_DEVTOOLS_LLVM_URL}"
    "${RV_DEVTOOLS_LLVM_SHA256}"
    "${RV_DEVTOOLS_LLVM_FILE}"
    "${RV_DEVTOOLS_DIR}/llvm"
    "${RV_DEVTOOLS_LLVM_NAME}"
    "${RV_DEVTOOLS_LLVM_PATHS}")

add_custom_target(3dmppc_devtools_tools
    DEPENDS 3dmppc_devtools_nvim 3dmppc_devtools_lua_language_server 3dmppc_devtools_llvm
)

# --- the 3dmppc devtools delivery (cmake/rv_devtools_delivery.cmake) ------------
# devtools-tree runs it in ALL on this build's outputs: it updates the tree in place in RV_PCONSOLE_DIR (the dev build's
# pconsole/), where the outer build has unpacked the SDK.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_log.cmake)

# The SDK the internal build compiles with; the delivery does not copy it, it must be ${RV_PCONSOLE_DIR}/steamrt4-sdk.
# Its toolchain sets the variable.
if(NOT IS_DIRECTORY "${RV_STEAMRT4_SDK}")
    message(FATAL_ERROR "no Steam Runtime 4 SDK directory ${RV_STEAMRT4_SDK}: the delivery takes it from RV_STEAMRT4_SDK")
endif()

# Neovim's archive ships no license: it is fetched from its tag, pinned by SHA-256. Without a dir the file is only
# downloaded and verified; the delivery copies it.
set(RV_DEVTOOLS_NVIM_LICENSE_URL "https://raw.githubusercontent.com/neovim/neovim/v${RV_NVIM_VERSION}/LICENSE.txt")
set(RV_DEVTOOLS_NVIM_LICENSE_SHA256 "de23202ef9a51f5e654034190539739a4abece5505f2ce75e780791106707011")
set(RV_DEVTOOLS_NVIM_LICENSE_FILE "neovim-${RV_NVIM_VERSION}-LICENSE.txt")
# Not in the release archive, so it is named apart from the Neovim download.
set(RV_DEVTOOLS_NVIM_LICENSE_NAME "Neovim ${RV_NVIM_VERSION} license")
rv_devtools_fetch(
    3dmppc_devtools_license_nvim
    "${RV_DEVTOOLS_NVIM_LICENSE_URL}"
    "${RV_DEVTOOLS_NVIM_LICENSE_SHA256}"
    "${RV_DEVTOOLS_NVIM_LICENSE_FILE}"
    ""
    "${RV_DEVTOOLS_NVIM_LICENSE_NAME}")

# Each license is a NAME=PATH pair, each version a line of VERSIONS.txt.
set(RV_DEVTOOLS_LICENSES
    "LuaJIT.txt=${luajit_SOURCE_DIR}/COPYRIGHT"
    "ImGui.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt"
    "libvterm.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm/LICENSE"
    "cgltf.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf/LICENSE"
    "stb.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/LICENSE"
    "PxPlus-IBM-VGA.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/pxplus-ibm-vga/LICENSE"
    "win55-icons.md=${CMAKE_CURRENT_SOURCE_DIR}/third_party/win55-icons/LICENSE.md"
    "Catppuccin.txt=${CMAKE_CURRENT_SOURCE_DIR}/third_party/catppuccin-palette/LICENSE"
    "LLVM.txt=${RV_DEVTOOLS_LLVM_DIR}/include/llvm/Support/LICENSE.TXT"
    "Neovim.txt=${RV_FETCH_CACHE}/${RV_DEVTOOLS_NVIM_LICENSE_FILE}"
    "lua-language-server.txt=${RV_DEVTOOLS_LUALS_DIR}/LICENSE")
set(RV_DEVTOOLS_VERSION_LINES
    "LuaJIT ${RV_LUAJIT_COMMIT}"
    "clang ${CMAKE_CXX_COMPILER_VERSION} (Steam Runtime 4 SDK)"
    "clangd ${RV_LLVM_VERSION}"
    "Neovim ${RV_NVIM_VERSION}"
    "lua-language-server ${RV_LUALS_VERSION}")

# Log paths are relative to the user's build dir (RV_LOG_BASE), else to this build dir, as in rv_devtools_fetch.
set(RV_DEVTOOLS_BASE "${RV_LOG_BASE}")
if("${RV_DEVTOOLS_BASE}" STREQUAL "")
    set(RV_DEVTOOLS_BASE "${CMAKE_BINARY_DIR}")
endif()
rv_log_relpath(RV_DEVTOOLS_TREE_REL "${RV_PCONSOLE_DIR}" "${RV_DEVTOOLS_BASE}")

set(RV_DEVTOOLS_DELIVERY_ARGS
    "-DRV_DEVTOOLS_BIN=${CMAKE_BINARY_DIR}/bin"
    "-DRV_DEVTOOLS_LAUNCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_launch.sh"
    "-DRV_DEVTOOLS_LLVM=${RV_DEVTOOLS_LLVM_DIR}"
    "-DRV_DEVTOOLS_NVIM=${RV_DEVTOOLS_NVIM_DIR}/nvim-linux-x86_64"
    "-DRV_DEVTOOLS_LUALS=${RV_DEVTOOLS_LUALS_DIR}"
    "-DRV_DEVTOOLS_COMPAT_CHECK=${CMAKE_BINARY_DIR}/tools/linux-compat-check.cmake"
    "-DRV_DEVTOOLS_TOOLCHAIN=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_steamrt4_toolchain.cmake"
    "-DRV_DEVTOOLS_STEAMRT4_SDK=${RV_STEAMRT4_SDK}"
    "-DRV_DEVTOOLS_RUNTIME=${RV_STEAMRT4_DIR}"
    "-DRV_DEVTOOLS_VER_MAJOR=${RV_MPPC_VER_MAJOR}"
    "-DRV_DEVTOOLS_VER_MINOR=${RV_MPPC_VER_MINOR}"
    "-DRV_DEVTOOLS_TREE=${RV_PCONSOLE_DIR}"
    "-DRV_DEVTOOLS_BASE=${RV_DEVTOOLS_BASE}"
    # The compat check's per-file report, written on every run (violations included); the check prints its path.
    "-DRV_DEVTOOLS_COMPAT_REPORT=${CMAKE_BINARY_DIR}/logs/compat-devtools.txt")
set(RV_DEVTOOLS_DELIVERY_DEPENDS
    3dmppc 3dmppc_editor 3dmppc_pdktools 3dmppc_firmware 3dmppc_webkit 3dmppc_devtools_tools
    3dmppc_devtools_license_nvim)

# The lists are quoted in COMMAND: an unquoted list would split the NAME=PATH pairs at their ';'.
add_custom_target(devtools-tree ALL
    DEPENDS ${RV_DEVTOOLS_DELIVERY_DEPENDS}
    COMMAND ${CMAKE_COMMAND} ${RV_DEVTOOLS_DELIVERY_ARGS}
            "-DRV_DEVTOOLS_LICENSES=${RV_DEVTOOLS_LICENSES}"
            "-DRV_DEVTOOLS_VERSIONS=${RV_DEVTOOLS_VERSION_LINES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_devtools_delivery.cmake"
    COMMENT "Updating the 3dmppc devtools delivery tree in ${RV_DEVTOOLS_TREE_REL}"
    VERBATIM
)
