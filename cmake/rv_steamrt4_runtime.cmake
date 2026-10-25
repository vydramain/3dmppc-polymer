# The Steam Linux Runtime 4 of the Linux delivery, as the inner build uses it. The host half (cmake/rv_steamrt4_host.cmake)
# fetches the pinned runtime once and passes its dir as RV_STEAMRT4_DIR; nothing here downloads.
# ${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4 links to it: mppcburner, in <build>/bin, finds the link and the copy of
# cmake/rv_linux_compat_check.cmake named <build>/tools/linux-compat-check.cmake (R/SteamLinuxRuntime_4 and R/tools/,
# R the parent of its dir).
# RV_FETCH_CACHE is the download cache of the external build (Steam Linux Runtime 4, SDK) and of the devtools delivery tools
# (cmake/rv_devtools_tools.cmake); it defaults under HOME.

if(NOT RV_STEAMRT4_DIR)
    message(FATAL_ERROR "RV_STEAMRT4_DIR is not set: configure without CMAKE_TOOLCHAIN_FILE, and the external build "
            "fetches the runtime and passes its dir")
endif()
if(NOT EXISTS "${RV_STEAMRT4_DIR}/pressure-vessel/bin/pressure-vessel-wrap")
    message(FATAL_ERROR "no ${RV_STEAMRT4_DIR}/pressure-vessel/bin/pressure-vessel-wrap: RV_STEAMRT4_DIR is not a "
            "Steam Linux Runtime 4; configure without CMAKE_TOOLCHAIN_FILE and the external build fetches it")
endif()

if(NOT RV_FETCH_CACHE)
    if(NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(rv_cache_base "$ENV{XDG_CACHE_HOME}")
    elseif(NOT "$ENV{HOME}" STREQUAL "")
        set(rv_cache_base "$ENV{HOME}/.cache")
    else()
        message(FATAL_ERROR "neither XDG_CACHE_HOME nor HOME is set, so there is no download cache: "
                "pass -DRV_FETCH_CACHE=<dir>")
    endif()
    # FORCE: an empty -DRV_FETCH_CACHE= is a cache entry that a plain CACHE set would keep.
    set(RV_FETCH_CACHE "${rv_cache_base}/3dmppc/downloads"
        CACHE PATH "Downloads of the pinned files (rv_fetch.cmake)" FORCE)
endif()

# Configure-time link, replaced on every configure.
file(REMOVE "${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4")
file(CREATE_LINK "${RV_STEAMRT4_DIR}" "${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4" SYMBOLIC RESULT rv_link_rc)
if(NOT rv_link_rc EQUAL 0)
    message(FATAL_ERROR "cannot link ${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4 to ${RV_STEAMRT4_DIR}: ${rv_link_rc}")
endif()
