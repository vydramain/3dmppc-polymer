cmake_minimum_required(VERSION 3.24)
project(3dmppc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- SDL3 ---------------------------------------------------------------------
# Use a system install if present, otherwise fetch and build it.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found on system — fetching from source.")
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# --- stb_image (header only) --------------------------------------------------
set(STB_DIR ${CMAKE_BINARY_DIR}/_deps/stb)
if(NOT EXISTS ${STB_DIR}/stb_image.h)
  message(STATUS "Downloading stb_image.h")
  file(DOWNLOAD
    https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
    ${STB_DIR}/stb_image.h
    TLS_VERIFY ON
  )
endif()

# --- phantasy development kit -------------------------------------------------

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk/include)

# --- game ---------------------------------------------------------------------
# CONFIGURE_DEPENDS: the build system re-checks the glob before each build, so
# a newly added .cpp is picked up without re-running cmake by hand.
file(GLOB_RECURSE 3DMPPC_CONSOLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(3dmppc
  # console (game-agnostic)
  ${3DMPPC_CONSOLE_SOURCES}
)

target_include_directories(3dmppc PRIVATE src ${STB_DIR})
target_link_libraries(3dmppc PRIVATE SDL3::SDL3)
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)

if(MSVC)
  target_compile_options(3dmppc PRIVATE /W4)
else()
  target_compile_options(3dmppc PRIVATE -Wall -Wextra)
endif()

# Copy the console's own demo assets next to the binary, if present. The disc is
# self-generating (procedural assets), so this dir is optional.
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
  add_custom_command(TARGET 3dmppc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:3dmppc>/assets
    COMMENT "Copying assets"
  )
endif()

# Copy the runtime files of the solidmaid disc (only what the demo loads — the
# source .glb / concept sketch stay out of the build tree).
add_custom_command(TARGET 3dmppc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:3dmppc>/mppcdiscs/solidmaid/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/mppcdiscs/solidmaid/assets/protagonist.obj
          ${CMAKE_SOURCE_DIR}/mppcdiscs/solidmaid/assets/protagonist_tex.png
          $<TARGET_FILE_DIR:3dmppc>/mppcdiscs/solidmaid/assets
  COMMENT "Copying solidmaid disc assets"
)
