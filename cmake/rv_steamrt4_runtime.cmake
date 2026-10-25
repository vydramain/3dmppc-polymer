# The pinned Steam Linux Runtime 4 of the Linux delivery. ${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4 is a link to
# RV_STEAMRT4_DIR, made at configure time; it dangles until 3dmppc_steamrt4_runtime fetches and extracts the tree
# into ${CMAKE_BINARY_DIR}/steamrt4. Nothing builds it by default: the dev build (3DMPPC_DEVTOOLS) and
# check-linux-compat depend on the target, so a player build does not download 166 MB it does not use.
# mppcburner, in <build>/pconsole, finds the link and the copy of cmake/rv_linux_compat_check.cmake in
# <build>/tools/ (R/SteamLinuxRuntime_4 and R/tools/ with R the parent of its dir).
# The download is kept in RV_FETCH_CACHE, which defaults under HOME (steamrt4.sh sets its own HOME); -DRV_FETCH_CACHE=<dir>
# moves it, so fresh build dirs reuse one download.

# Snapshot of the latest-public-stable channel, shared by Steam Linux Runtime 4 and the Steam Runtime 4 SDK
# (cmake/rv_devkit_tools.cmake): both URLs and file names are built from RV_STEAMRT4_VERSION.
set(RV_STEAMRT4_VERSION "4.0.20261021.265997")
set(RV_STEAMRT4_URL "https://repo.steampowered.com/steamrt4/images/${RV_STEAMRT4_VERSION}/SteamLinuxRuntime_4.tar.xz")
set(RV_STEAMRT4_SHA256 "aa9dd1aab2173e65b631faae34b85e2c17fe323d941013154ddd2e1dce8a0910")
set(RV_STEAMRT4_FILE "SteamLinuxRuntime_4-${RV_STEAMRT4_VERSION}.tar.xz")
set(RV_STEAMRT4_DIR "${CMAKE_BINARY_DIR}/steamrt4/SteamLinuxRuntime_4")

if(NOT RV_FETCH_CACHE)
    if(NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(rv_cache_base "$ENV{XDG_CACHE_HOME}")
    elseif(NOT "$ENV{HOME}" STREQUAL "")
        set(rv_cache_base "$ENV{HOME}/.cache")
    else()
        message(FATAL_ERROR "neither XDG_CACHE_HOME nor HOME is set, so there is no download cache: "
                "pass -DRV_FETCH_CACHE=<dir>")
    endif()
    set(RV_FETCH_CACHE "${rv_cache_base}/3dmppc/downloads" CACHE PATH "Downloads of the pinned files (rv_fetch.cmake)")
endif()

# Configure-time link: a dangling one is fine until the fetch.
file(REMOVE "${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4")
file(CREATE_LINK "${RV_STEAMRT4_DIR}" "${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4" SYMBOLIC)

# The fetch dir is steamrt4/, never the build dir itself: rv_fetch replaces its target dir.
add_custom_target(3dmppc_steamrt4_runtime
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RV_FETCH_CACHE}"
    COMMAND ${CMAKE_COMMAND}
            -DRV_FETCH_URL=${RV_STEAMRT4_URL}
            -DRV_FETCH_SHA256=${RV_STEAMRT4_SHA256}
            "-DRV_FETCH_FILE=${RV_FETCH_CACHE}/${RV_STEAMRT4_FILE}"
            "-DRV_FETCH_DIR=${CMAKE_BINARY_DIR}/steamrt4"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_fetch.cmake"
    COMMENT "Fetching the pinned Steam Linux Runtime 4"
    VERBATIM
)
