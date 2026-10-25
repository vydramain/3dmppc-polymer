# The pinned development kit tools of the dev build (3DMPPC_DEVTOOLS on Linux, not Emscripten): CMake, Ninja, Neovim,
# lua-language-server and the Steam Runtime 4 SDK sysroot. mppcburner builds disc.so inside Steam Linux Runtime 4 with no
# distribution tools, so the sysroot ships with the 3dmppc development kit delivery; the editor runs Neovim and the
# language servers.
# Each tool is fetched and extracted by cmake/rv_fetch.cmake into its own dir under ${CMAKE_BINARY_DIR}/devkit/ by
# the 3dmppc_devkit_<tool> target; 3dmppc_devkit_tools builds them all. Nothing builds them by default.
# Needs RV_FETCH_CACHE and RV_STEAMRT4_VERSION, which cmake/rv_steamrt4_runtime.cmake sets.

# CMake 3.31.6, Kitware release v3.31.6; SHA-256 from Kitware's cmake-3.31.6-SHA-256.txt.
set(RV_DEVKIT_CMAKE_URL "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz")
set(RV_DEVKIT_CMAKE_SHA256 "5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf")
set(RV_DEVKIT_CMAKE_FILE "cmake-3.31.6-linux-x86_64.tar.gz")

# Ninja 1.12.1, ninja-build release v1.12.1; GitHub publishes no SHA-256, so it was computed at download.
set(RV_DEVKIT_NINJA_URL "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-linux.zip")
set(RV_DEVKIT_NINJA_SHA256 "6f98805688d19672bd699fbbfa2c2cf0fc054ac3df1f0e6a47664d963d530255")
set(RV_DEVKIT_NINJA_FILE "ninja-1.12.1-linux.zip")

# Neovim 0.12.6, neovim release v0.12.6.
set(RV_DEVKIT_NVIM_URL "https://github.com/neovim/neovim/releases/download/v0.12.6/nvim-linux-x86_64.tar.gz")
set(RV_DEVKIT_NVIM_SHA256 "474430d53e6264f6d6dd18db42d6dc9df3a1b56ca9e88a325bbf860e1a811d87")
set(RV_DEVKIT_NVIM_FILE "nvim-0.12.6-linux-x86_64.tar.gz")

# lua-language-server 3.19.2, LuaLS release 3.19.2.
set(RV_DEVKIT_LUALS_BASE "https://github.com/LuaLS/lua-language-server/releases/download/3.19.2/")
set(RV_DEVKIT_LUALS_URL "${RV_DEVKIT_LUALS_BASE}lua-language-server-3.19.2-linux-x64.tar.gz")
set(RV_DEVKIT_LUALS_SHA256 "93ad326b10f6c00911dd8c450fa098332dbd8f9f45d1052f5636a2d54479b947")
set(RV_DEVKIT_LUALS_FILE "lua-language-server-3.19.2-linux-x64.tar.gz")

# Steam Runtime 4 SDK sysroot archive, the same snapshot as Steam Linux Runtime 4 (RV_STEAMRT4_VERSION).
# Its top entry is ./, so the extracted dir is the sysroot root.
set(RV_DEVKIT_SYSROOT_BASE "https://repo.steampowered.com/steamrt4/images/${RV_STEAMRT4_VERSION}/")
set(RV_DEVKIT_SYSROOT_URL "${RV_DEVKIT_SYSROOT_BASE}com.valvesoftware.SteamRuntime.Sdk-amd64%2Ci386-steamrt4-sysroot.tar.gz")
set(RV_DEVKIT_SYSROOT_SHA256 "c01a7507cea248c231412f3d70563501899c3e77a8c9f08078a7c7cf30bee6a6")
set(RV_DEVKIT_SYSROOT_FILE "steamrt4-sdk-sysroot-${RV_STEAMRT4_VERSION}.tar.gz")

# Extracted dirs, for the packaging that ships them.
set(RV_DEVKIT_DIR "${CMAKE_BINARY_DIR}/devkit")
set(RV_DEVKIT_CMAKE_DIR "${RV_DEVKIT_DIR}/cmake")
set(RV_DEVKIT_NINJA_DIR "${RV_DEVKIT_DIR}/ninja")
set(RV_DEVKIT_NVIM_DIR "${RV_DEVKIT_DIR}/nvim")
set(RV_DEVKIT_LUALS_DIR "${RV_DEVKIT_DIR}/lua-language-server")
set(RV_DEVKIT_SYSROOT_DIR "${RV_DEVKIT_DIR}/steamrt4-sdk")

# One target per tool: fetch into the shared cache, verify the pin, extract into dir when dir is set.
function(rv_devkit_fetch target url sha256 file dir)
    add_custom_target(${target}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RV_FETCH_CACHE}"
        COMMAND ${CMAKE_COMMAND}
                -DRV_FETCH_URL=${url}
                -DRV_FETCH_SHA256=${sha256}
                "-DRV_FETCH_FILE=${RV_FETCH_CACHE}/${file}"
                "-DRV_FETCH_DIR=${dir}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_fetch.cmake"
        COMMENT "Fetching ${file}"
        VERBATIM
    )
endfunction()

rv_devkit_fetch(3dmppc_devkit_cmake "${RV_DEVKIT_CMAKE_URL}" "${RV_DEVKIT_CMAKE_SHA256}" "${RV_DEVKIT_CMAKE_FILE}"
    "${RV_DEVKIT_CMAKE_DIR}")
rv_devkit_fetch(3dmppc_devkit_ninja "${RV_DEVKIT_NINJA_URL}" "${RV_DEVKIT_NINJA_SHA256}" "${RV_DEVKIT_NINJA_FILE}"
    "${RV_DEVKIT_NINJA_DIR}")
rv_devkit_fetch(3dmppc_devkit_nvim "${RV_DEVKIT_NVIM_URL}" "${RV_DEVKIT_NVIM_SHA256}" "${RV_DEVKIT_NVIM_FILE}"
    "${RV_DEVKIT_NVIM_DIR}")
rv_devkit_fetch(3dmppc_devkit_lua_language_server "${RV_DEVKIT_LUALS_URL}" "${RV_DEVKIT_LUALS_SHA256}"
    "${RV_DEVKIT_LUALS_FILE}" "${RV_DEVKIT_LUALS_DIR}")
rv_devkit_fetch(3dmppc_devkit_steamrt4_sdk "${RV_DEVKIT_SYSROOT_URL}" "${RV_DEVKIT_SYSROOT_SHA256}"
    "${RV_DEVKIT_SYSROOT_FILE}" "${RV_DEVKIT_SYSROOT_DIR}")

add_custom_target(3dmppc_devkit_tools
    DEPENDS 3dmppc_devkit_cmake 3dmppc_devkit_ninja 3dmppc_devkit_nvim 3dmppc_devkit_lua_language_server
            3dmppc_devkit_steamrt4_sdk
)

# --- the 3dmppc development kit delivery (cmake/rv_devkit_delivery.cmake) -------
# devkit-tree and package-devkit run it on this build's outputs; neither is in ALL.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)

# Neovim and Ninja archives ship no license: each is fetched from its tag, pinned by SHA-256. Without a dir the file is
# only downloaded and verified; the delivery copies it.
set(RV_DEVKIT_NVIM_LICENSE_URL "https://raw.githubusercontent.com/neovim/neovim/v0.12.6/LICENSE.txt")
set(RV_DEVKIT_NVIM_LICENSE_SHA256 "de23202ef9a51f5e654034190539739a4abece5505f2ce75e780791106707011")
set(RV_DEVKIT_NVIM_LICENSE_FILE "neovim-0.12.6-LICENSE.txt")
set(RV_DEVKIT_NINJA_LICENSE_URL "https://raw.githubusercontent.com/ninja-build/ninja/v1.12.1/COPYING")
set(RV_DEVKIT_NINJA_LICENSE_SHA256 "eb7e9ab9690124c5c9f42bdc81383d886a3dede26345b6ed15bbad7caf81f7ea")
set(RV_DEVKIT_NINJA_LICENSE_FILE "ninja-1.12.1-COPYING")
rv_devkit_fetch(3dmppc_devkit_license_nvim "${RV_DEVKIT_NVIM_LICENSE_URL}" "${RV_DEVKIT_NVIM_LICENSE_SHA256}"
    "${RV_DEVKIT_NVIM_LICENSE_FILE}" "")
rv_devkit_fetch(3dmppc_devkit_license_ninja "${RV_DEVKIT_NINJA_LICENSE_URL}" "${RV_DEVKIT_NINJA_LICENSE_SHA256}"
    "${RV_DEVKIT_NINJA_LICENSE_FILE}" "")

# The LLVM that builds discs. Assumes the compiler is LLVM's bin/clang-23 (the steamrt4.sh build), so its parent's parent
# is the LLVM top dir; otherwise the delivery's input check fails loudly.
get_filename_component(RV_DEVKIT_LLVM_DIR "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(RV_DEVKIT_LLVM_DIR "${RV_DEVKIT_LLVM_DIR}" DIRECTORY)
get_filename_component(RV_DEVKIT_LLVM_DIR "${RV_DEVKIT_LLVM_DIR}" DIRECTORY)

# The extracted CMake tree; the version lines below must match the pins above.
set(RV_DEVKIT_CMAKE_ROOT "${RV_DEVKIT_CMAKE_DIR}/cmake-3.31.6-linux-x86_64")

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
    "CMake.txt=${RV_DEVKIT_CMAKE_ROOT}/doc/cmake/Copyright.txt"
    "Ninja.txt=${RV_FETCH_CACHE}/${RV_DEVKIT_NINJA_LICENSE_FILE}"
    "Neovim.txt=${RV_FETCH_CACHE}/${RV_DEVKIT_NVIM_LICENSE_FILE}"
    "lua-language-server.txt=${RV_DEVKIT_LUALS_DIR}/LICENSE")
set(RV_DEVKIT_VERSION_LINES
    "LuaJIT ${RV_LUAJIT_COMMIT}"
    "LLVM ${CMAKE_CXX_COMPILER_VERSION}"
    "CMake 3.31.6"
    "Ninja 1.12.1"
    "Neovim 0.12.6"
    "lua-language-server 3.19.2")

set(RV_DEVKIT_DELIVERY_TREE "${CMAKE_BINARY_DIR}/devkit-delivery/3dmppc-devkit")
set(RV_DEVKIT_DELIVERY_ZIP "${CMAKE_BINARY_DIR}/3dmppc-devkit-${RV_MPPC_VER_MAJOR}.${RV_MPPC_VER_MINOR}.zip")
set(RV_DEVKIT_DELIVERY_ARGS
    "-DRV_DEVKIT_BIN=${CMAKE_BINARY_DIR}/pconsole"
    "-DRV_DEVKIT_LAUNCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_launch.sh"
    "-DRV_DEVKIT_LLVM=${RV_DEVKIT_LLVM_DIR}"
    "-DRV_DEVKIT_CMAKE=${RV_DEVKIT_CMAKE_ROOT}"
    "-DRV_DEVKIT_NINJA=${RV_DEVKIT_NINJA_DIR}/ninja"
    "-DRV_DEVKIT_NVIM=${RV_DEVKIT_NVIM_DIR}/nvim-linux-x86_64"
    "-DRV_DEVKIT_LUALS=${RV_DEVKIT_LUALS_DIR}"
    "-DRV_DEVKIT_COMPAT_CHECK=${CMAKE_BINARY_DIR}/tools/linux-compat-check.cmake"
    "-DRV_DEVKIT_TOOLCHAIN=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_steamrt4_toolchain.cmake"
    "-DRV_DEVKIT_SYSROOT=${RV_DEVKIT_SYSROOT_DIR}"
    "-DRV_DEVKIT_RUNTIME=${RV_STEAMRT4_DIR}"
    "-DRV_DEVKIT_VER_MAJOR=${RV_MPPC_VER_MAJOR}"
    "-DRV_DEVKIT_VER_MINOR=${RV_MPPC_VER_MINOR}"
    "-DRV_DEVKIT_TREE=${RV_DEVKIT_DELIVERY_TREE}")
set(RV_DEVKIT_DELIVERY_DEPENDS
    3dmppc 3dmppc_editor 3dmppc_pdktools 3dmppc_firmware 3dmppc_webkit 3dmppc_steamrt4_runtime 3dmppc_devkit_tools
    3dmppc_devkit_license_nvim 3dmppc_devkit_license_ninja)

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
