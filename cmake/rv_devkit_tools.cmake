# The pinned development kit tools of the dev build (3DMPPC_DEVTOOLS on Linux, not Emscripten): CMake, Ninja, Neovim,
# lua-language-server and the Steam Runtime 4 SDK sysroot. mppcburner builds disc.so inside Steam Linux Runtime 4 with no
# distribution tools, so the sysroot ships with the 3dmppc development kit delivery; the editor runs Neovim and the
# language servers.
# Each tool is fetched and extracted by cmake/rv_fetch.cmake into its own dir under ${CMAKE_BINARY_DIR}/devkit/ by
# the 3dmppc_devkit_<tool> target; 3dmppc_devkit_tools builds them all. Nothing builds them by default.
# Needs RV_FETCH_CACHE, which cmake/rv_steamrt4_runtime.cmake sets.

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

# Steam Runtime 4 SDK sysroot archive, snapshot 4.0.20261021.265997 (the same snapshot as Steam Linux Runtime 4).
# Its top entry is ./, so the extracted dir is the sysroot root.
set(RV_DEVKIT_SYSROOT_BASE "https://repo.steampowered.com/steamrt4/images/4.0.20261021.265997/")
set(RV_DEVKIT_SYSROOT_URL "${RV_DEVKIT_SYSROOT_BASE}com.valvesoftware.SteamRuntime.Sdk-amd64%2Ci386-steamrt4-sysroot.tar.gz")
set(RV_DEVKIT_SYSROOT_SHA256 "c01a7507cea248c231412f3d70563501899c3e77a8c9f08078a7c7cf30bee6a6")
set(RV_DEVKIT_SYSROOT_FILE "steamrt4-sdk-sysroot-4.0.20261021.265997.tar.gz")

# Extracted dirs, for the packaging that ships them.
set(RV_DEVKIT_DIR "${CMAKE_BINARY_DIR}/devkit")
set(RV_DEVKIT_CMAKE_DIR "${RV_DEVKIT_DIR}/cmake")
set(RV_DEVKIT_NINJA_DIR "${RV_DEVKIT_DIR}/ninja")
set(RV_DEVKIT_NVIM_DIR "${RV_DEVKIT_DIR}/nvim")
set(RV_DEVKIT_LUALS_DIR "${RV_DEVKIT_DIR}/lua-language-server")
set(RV_DEVKIT_SYSROOT_DIR "${RV_DEVKIT_DIR}/steamrt4-sdk")

# One target per tool: fetch into the shared cache, verify the pin, extract into dir.
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
