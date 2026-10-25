include_guard(GLOBAL)

include(FetchContent)

# --- SDL3 ---------------------------------------------------------------------
# Use a system install if present, otherwise fetch and build it.
# "System" is the build's sysroot: the Steam Runtime 4 SDK on Linux.
# The URL is the official SDL repository; a build that downloads SDL3 fetches the pinned tag.
set(RV_SDL3_GIT_URL https://github.com/libsdl-org/SDL.git)
set(RV_SDL3_GIT_TAG release-3.4.16)

# A nested build gets the parent's SDL3 source if the parent has one (sdl3_SOURCE_DIR).
if(FETCHCONTENT_SOURCE_DIR_SDL3)
  set(RV_SDL3_SOURCE_NOTE "given by FETCHCONTENT_SOURCE_DIR_SDL3 at ${FETCHCONTENT_SOURCE_DIR_SDL3}, not downloaded")
else()
  string(CONCAT
    RV_SDL3_SOURCE_NOTE
    "downloaded from ${RV_SDL3_GIT_URL} tag ${RV_SDL3_GIT_TAG} "
    "to ${FETCHCONTENT_BASE_DIR}/sdl3-src unless an earlier configure fetched this tag")
endif()

find_package(SDL3 QUIET)
if(SDL3_FOUND)
  message(STATUS "SDL3 ${SDL3_VERSION} found in ${SDL3_DIR} and used")
else()
  message(STATUS "SDL3 is not installed for this build: building it statically with the project, source ${RV_SDL3_SOURCE_NOTE}")
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY ${RV_SDL3_GIT_URL}
    GIT_TAG ${RV_SDL3_GIT_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()
