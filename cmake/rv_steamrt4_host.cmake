# Host half of the Linux build. CMakeLists.txt enters here before project() when the host is Linux and no toolchain
# is given: it checks the host programs, fetches the pinned Steam Linux Runtime 4 and Steam Runtime 4 SDK, and
# configures the project inside the SDK in ${CMAKE_BINARY_DIR}/sdk. It compiles nothing; the inner configure and build do.
# Usage: cmake -S <repo> -B <build> -G Ninja [-D3DMPPC_*=...] [-DCMAKE_BUILD_TYPE=...] [-DRV_FETCH_CACHE=<dir>]

include(${CMAKE_CURRENT_LIST_DIR}/rv_log.cmake)

# One snapshot names both downloads (the Steam Linux Runtime 4 and the Steam Runtime 4 SDK sysroot).
set(RV_STEAMRT4_VERSION "4.0.20261021.265997")
set(RV_STEAMRT4_URL "https://repo.steampowered.com/steamrt4/images/${RV_STEAMRT4_VERSION}/SteamLinuxRuntime_4.tar.xz")
set(RV_STEAMRT4_SHA256 "aa9dd1aab2173e65b631faae34b85e2c17fe323d941013154ddd2e1dce8a0910")
set(RV_STEAMRT4_FILE "SteamLinuxRuntime_4-${RV_STEAMRT4_VERSION}.tar.xz")
# The SDK archive's top entry is ./, so it extracts to the sysroot root.
set(RV_STEAMRT4_SDK_BASE "https://repo.steampowered.com/steamrt4/images/${RV_STEAMRT4_VERSION}/")
set(RV_STEAMRT4_SDK_URL "${RV_STEAMRT4_SDK_BASE}com.valvesoftware.SteamRuntime.Sdk-amd64%2Ci386-steamrt4-sysroot.tar.gz")
set(RV_STEAMRT4_SDK_SHA256 "c01a7507cea248c231412f3d70563501899c3e77a8c9f08078a7c7cf30bee6a6")
set(RV_STEAMRT4_SDK_FILE "steamrt4-sdk-sysroot-${RV_STEAMRT4_VERSION}.tar.gz")
# Names of the downloads: rv_fetch.cmake prints each one as the header of its stage.
set(RV_STEAMRT4_NAME "Steam Linux Runtime 4 ${RV_STEAMRT4_VERSION}")
set(RV_STEAMRT4_SDK_NAME "Steam Runtime 4 SDK ${RV_STEAMRT4_VERSION}")

# Build-tree dirs, declared once. rv_fetch.cmake replaces its target dir as a whole, so the runtime is reached through a link.
# <build>/pconsole/ is this build's delivery (player or devtools); this file passes it to the inner build as RV_PCONSOLE_DIR.
set(RV_PCONSOLE_DIR "${CMAKE_BINARY_DIR}/pconsole")
set(RV_HOST_STEAMRT4_DIR "${CMAKE_BINARY_DIR}/steamrt4")
set(RV_HOST_RUNTIME "${CMAKE_BINARY_DIR}/SteamLinuxRuntime_4")
set(RV_HOST_INNER "${CMAKE_BINARY_DIR}/sdk")
# The SDK is always unpacked. With 3DMPPC_DEVTOOLS it goes to pconsole/steamrt4-sdk, since the SDK is part of the
# devtools delivery: the inner build and its runs take it from there, with no second copy. Without it, it goes to
# <build>/steamrt4-sdk, since the player delivery does not include the SDK. Switching 3DMPPC_DEVTOOLS in the same build
# dir leaves the old SDK unpack in place (3.4 GB); delete it by hand.
if(3DMPPC_DEVTOOLS)
    set(RV_STEAMRT4_SDK_DIR "${RV_PCONSOLE_DIR}/steamrt4-sdk")
else()
    set(RV_STEAMRT4_SDK_DIR "${CMAKE_BINARY_DIR}/steamrt4-sdk")
endif()

# Downloads of the pinned files, outside the build dir. rv_fetch.cmake deletes an archive after a successful extraction;
# it keeps the archive of a failed extraction and the partial file of an interrupted download; a fresh build dir resumes them.
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
# pressure-vessel's --filesystem needs an absolute path.
get_filename_component(RV_FETCH_CACHE "${RV_FETCH_CACHE}" ABSOLUTE)

# curl is the only host program the downloads need; rv_fetch.cmake gets the same binary through RV_CURL.
find_program(RV_CURL curl)
if(NOT RV_CURL)
    message(FATAL_ERROR "curl is not found: it downloads the Steam Runtime 4 files; install the curl package")
endif()

set(RV_HOST_FETCH_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/rv_fetch.cmake")

# Fetches one pin into the cache and extracts it into dir; rv_fetch.cmake skips a dir that already matches its stamp.
function(rv_host_fetch url sha256 name dir phrase)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DRV_FETCH_URL=${url}"
                "-DRV_FETCH_SHA256=${sha256}"
                "-DRV_FETCH_FILE=${RV_FETCH_CACHE}/${name}"
                "-DRV_FETCH_DIR=${dir}"
                "-DRV_FETCH_NAME=${phrase}"
                "-DRV_FETCH_BASE=${CMAKE_BINARY_DIR}"
                "-DRV_CURL=${RV_CURL}"
                -P "${RV_HOST_FETCH_SCRIPT}"
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "fetching ${name} failed (exit ${rc}): the message above says why; the next configure resumes it")
    endif()
endfunction()

rv_log_step("the Linux build runs in the Steam Runtime 4 SDK through pressure-vessel: fetch the runtime, then the SDK")
file(MAKE_DIRECTORY "${RV_FETCH_CACHE}")
rv_host_fetch(
    "${RV_STEAMRT4_URL}"
    "${RV_STEAMRT4_SHA256}"
    "${RV_STEAMRT4_FILE}"
    "${RV_HOST_STEAMRT4_DIR}"
    "${RV_STEAMRT4_NAME}")
rv_host_fetch(
    "${RV_STEAMRT4_SDK_URL}"
    "${RV_STEAMRT4_SDK_SHA256}"
    "${RV_STEAMRT4_SDK_FILE}"
    "${RV_STEAMRT4_SDK_DIR}"
    "${RV_STEAMRT4_SDK_NAME}")

file(REMOVE "${RV_HOST_RUNTIME}")
file(CREATE_LINK "${RV_HOST_STEAMRT4_DIR}/SteamLinuxRuntime_4" "${RV_HOST_RUNTIME}" SYMBOLIC RESULT link_rc)
if(NOT link_rc EQUAL 0)
    message(FATAL_ERROR "cannot link ${RV_HOST_RUNTIME} to ${RV_HOST_STEAMRT4_DIR}/SteamLinuxRuntime_4: ${link_rc}")
endif()

# The wrap runs from the fetched runtime and needs the host's user namespaces; nothing here checks that.
set(RV_HOST_WRAP "${RV_HOST_RUNTIME}/pressure-vessel/bin/pressure-vessel-wrap")
if(NOT EXISTS "${RV_HOST_WRAP}")
    message(FATAL_ERROR "no ${RV_HOST_WRAP}: the extracted Steam Linux Runtime 4 has no pressure-vessel-wrap")
endif()

# The inner configure gets the project options the user gave, the build type and the download cache.
get_cmake_property(cache_vars CACHE_VARIABLES)
set(inner_opts "-DRV_FETCH_CACHE=${RV_FETCH_CACHE}")
foreach(var IN LISTS cache_vars)
    if("${var}" MATCHES "^3DMPPC_")
        list(APPEND inner_opts "-D${var}=${${var}}")
    endif()
endforeach()
if(CMAKE_BUILD_TYPE)
    list(APPEND inner_opts "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

# pressure-vessel mounts the SDK as the runtime; the source, build and download cache dirs are the paths it must open
# (the devtools download targets of the inner build fetch into the cache).
# The configure and the build targets share this wrap.
set(RV_HOST_WRAP_ARGS
    "${RV_HOST_WRAP}"
    "--runtime=${RV_STEAMRT4_SDK_DIR}"
    --batch
    "--filesystem=${CMAKE_SOURCE_DIR}"
    "--filesystem=${CMAKE_BINARY_DIR}"
    "--filesystem=${RV_FETCH_CACHE}"
    --
)

rv_log_relpath(inner_rel "${RV_HOST_INNER}" "${CMAKE_BINARY_DIR}")
rv_log_header("Inner configure in ${inner_rel}/ (Steam Runtime 4 SDK)")
rv_log_step("pressure-vessel's warning \"Not making .../ld.so.conf a symlink\" is expected: the SDK is used in place")
execute_process(
    COMMAND ${RV_HOST_WRAP_ARGS} cmake -S "${CMAKE_SOURCE_DIR}" -B "${RV_HOST_INNER}" -G Ninja
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/rv_steamrt4_toolchain.cmake"
            "-DRV_STEAMRT4_SDK=${RV_STEAMRT4_SDK_DIR}"
            "-DRV_STEAMRT4_DIR=${RV_HOST_STEAMRT4_DIR}/SteamLinuxRuntime_4"
            "-DRV_PCONSOLE_DIR=${RV_PCONSOLE_DIR}"
            ${inner_opts}
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the inner configure in ${RV_HOST_INNER} failed (exit ${rc})")
endif()
rv_log_step("the inner build in ${inner_rel}/ runs at the build step; this directory only passes cmake --build to it")

# 3dmppc_inner is the only target here: it runs cmake --build on the inner build dir, which builds its default target.
# Its build also prints the pressure-vessel warning; configure has explained it already.
add_custom_target(3dmppc_inner ALL
    COMMAND ${RV_HOST_WRAP_ARGS} cmake --build "${RV_HOST_INNER}"
    COMMENT "Building the inner default target in ${inner_rel}/ (Steam Runtime 4 SDK)"
    USES_TERMINAL VERBATIM)
