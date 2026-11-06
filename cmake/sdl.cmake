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
  FetchContent_Declare(
    SDL3
    URL ${RV_SDL3_URL}
    URL_HASH SHA256=${RV_SDL3_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )
  FetchContent_MakeAvailable(SDL3)
endif()
