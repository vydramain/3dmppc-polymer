include_guard(GLOBAL)

include(FetchContent)

# --- SDL3 ---------------------------------------------------------------------
# Use a system install if present, otherwise fetch and build it.
# "System" is the build's sysroot: the Steam Runtime 4 SDK on Linux.
# A build that downloads SDL3 fetches the release archive, not a git clone: the archive has no SDL history
# to fetch and URL_HASH checks its content. RV_SDL3_VERSION must equal the Steam Runtime 4 SDK's SDL.
set(RV_SDL3_VERSION 3.4.18)
set(RV_SDL3_TAG release-${RV_SDL3_VERSION})
set(RV_SDL3_URL https://github.com/libsdl-org/SDL/releases/download/${RV_SDL3_TAG}/SDL3-${RV_SDL3_VERSION}.tar.gz)
# SHA256 of that archive's content: the fetch fails if the release changes.
set(RV_SDL3_SHA256 9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)

# The WebHID bridge of SDL's hidapi for the browser: an Emscripten --js-library.
set(RV_WEB_JS_LIBRARY ${CMAKE_CURRENT_LIST_DIR}/sdl3/rv_webhid.js)

# A nested build gets the parent's SDL3 source if the parent has one (sdl3_SOURCE_DIR).
if(FETCHCONTENT_SOURCE_DIR_SDL3)
  set(RV_SDL3_SOURCE_NOTE "given by FETCHCONTENT_SOURCE_DIR_SDL3 at ${FETCHCONTENT_SOURCE_DIR_SDL3}, not downloaded")
else()
  string(CONCAT
    RV_SDL3_SOURCE_NOTE
    "downloaded from ${RV_SDL3_URL} (SDL ${RV_SDL3_VERSION}) "
    "to ${FETCHCONTENT_BASE_DIR}/sdl3-src unless an earlier configure fetched this archive")
endif()

find_package(SDL3 QUIET)
if(SDL3_FOUND)
  message(STATUS "SDL3 ${SDL3_VERSION} found in ${SDL3_DIR} and used")
  # The version check reads the found SDL3's SDL_version.h from its headers target.
  if(NOT TARGET SDL3::Headers)
    message(WARNING "check-sdl3-version is not available: the found SDL3 has no SDL3::Headers target")
  else()
    get_target_property(RV_SDL3_INCLUDE_DIRS SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT RV_SDL3_INCLUDE_DIRS)
      message(WARNING "check-sdl3-version is not available: SDL3::Headers has no INTERFACE_INCLUDE_DIRECTORIES")
    else()
      list(GET RV_SDL3_INCLUDE_DIRS 0 RV_SDL3_INCLUDE_DIR)
      # Run it with: cmake --build <build-dir> --target check-sdl3-version
      # See cmake/rv_sdl3_check.cmake for the rule.
      add_custom_target(check-sdl3-version
        COMMAND ${CMAKE_COMMAND}
                "-DRV_SDL3_VERSION=${RV_SDL3_VERSION}"
                "-DRV_SDL3_HEADER=${RV_SDL3_INCLUDE_DIR}/SDL3/SDL_version.h"
                -P "${CMAKE_CURRENT_LIST_DIR}/rv_sdl3_check.cmake"
        COMMENT "Checking the SDL3 version against RV_SDL3_VERSION"
        VERBATIM
      )
    endif()
  endif()
else()
  # The fetched source is pinned by the archive hash, so there is no version check target.
  message(STATUS "SDL3 is not installed for this build: building it statically with the project, source ${RV_SDL3_SOURCE_NOTE}")
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  # The patch step lays the SDL patch set on the source. The hash of the set and of its script is in the command, so a
  # change of the set runs the step again. Adding or removing a set file reconfigures at the next build (CONFIGURE_DEPENDS).
  set(RV_SDL3_PATCH_DIR ${CMAKE_CURRENT_LIST_DIR}/sdl3/${RV_SDL3_TAG})
  set(RV_SDL3_PATCH_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/rv_sdl3_patch.cmake)
  # The same names the script skips: dot files and backups are not part of the set.
  file(GLOB_RECURSE rv_sdl3_set_files LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
       RELATIVE "${RV_SDL3_PATCH_DIR}" "${RV_SDL3_PATCH_DIR}/*")
  list(FILTER rv_sdl3_set_files EXCLUDE REGEX "(^|/)\\.|~(/|$)")
  list(SORT rv_sdl3_set_files)
  set(rv_sdl3_set_paths ${RV_SDL3_PATCH_SCRIPT})
  set(rv_sdl3_set_listing "")
  foreach(rel IN LISTS rv_sdl3_set_files)
    file(SHA256 "${RV_SDL3_PATCH_DIR}/${rel}" rv_sdl3_file_hash)
    string(APPEND rv_sdl3_set_listing "${rv_sdl3_file_hash} ${rel}\n")
    list(APPEND rv_sdl3_set_paths "${RV_SDL3_PATCH_DIR}/${rel}")
  endforeach()
  file(SHA256 "${RV_SDL3_PATCH_SCRIPT}" rv_sdl3_script_hash)
  string(SHA256 RV_SDL3_PATCH_HASH "${rv_sdl3_script_hash}\n${rv_sdl3_set_listing}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${rv_sdl3_set_paths})
  FetchContent_Declare(
    SDL3
    URL ${RV_SDL3_URL}
    URL_HASH SHA256=${RV_SDL3_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    # The archive is the download step's file, named by its URL; a nested build given FETCHCONTENT_SOURCE_DIR_SDL3 never
    # runs this step. <DOWNLOADED_FILE> is empty here, so the name comes from <DOWNLOAD_DIR>.
    PATCH_COMMAND ${CMAKE_COMMAND}
                  -DRV_SDL3_SRC=<SOURCE_DIR>
                  -DRV_SDL3_PATCH_DIR=${RV_SDL3_PATCH_DIR}
                  -DRV_SDL3_VERSION=${RV_SDL3_VERSION}
                  -DRV_SDL3_TAG=${RV_SDL3_TAG}
                  -DRV_SDL3_ARCHIVE=<DOWNLOAD_DIR>/SDL3-${RV_SDL3_VERSION}.tar.gz
                  -DRV_SDL3_SHA256=${RV_SDL3_SHA256}
                  -DRV_SDL3_PATCH_HASH=${RV_SDL3_PATCH_HASH}
                  -P ${RV_SDL3_PATCH_SCRIPT}
  )
  FetchContent_MakeAvailable(SDL3)
endif()
