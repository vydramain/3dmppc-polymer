# 3dmppc-editor: the desktop application a game is authored in. A separate
# product from the console and the tools: it drives them as processes and links
# none of their code.
#
#   cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
#
# A development build of the console (-D3DMPPC_DEVTOOLS=ON) builds it as well.

cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(3dmppc_editor LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(RV_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

include(${RV_REPO_ROOT}/cmake/sdl.cmake)
include(${RV_REPO_ROOT}/cmake/pdklib.cmake)

# --- Dear ImGui (vendored) ----------------------------------------------------
# third_party/imgui/ORIGIN.md says what was taken and why. ImGui has no build
# system of its own: the consumer compiles its files, which is what this target
# does. SYSTEM keeps ImGui's warnings out of the editor's own.
set(RV_IMGUI_DIR ${RV_REPO_ROOT}/third_party/imgui)
add_library(3dmppc_editor_imgui STATIC
  ${RV_IMGUI_DIR}/imgui.cpp
  ${RV_IMGUI_DIR}/imgui_draw.cpp
  ${RV_IMGUI_DIR}/imgui_tables.cpp
  ${RV_IMGUI_DIR}/imgui_widgets.cpp
  ${RV_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${RV_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(3dmppc_editor_imgui SYSTEM PUBLIC ${RV_IMGUI_DIR} ${RV_IMGUI_DIR}/backends)
# Build options come from the editor's own header, not from edits to imconfig.h.
target_include_directories(3dmppc_editor_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(3dmppc_editor_imgui PUBLIC IMGUI_USER_CONFIG="rv_editor_imconfig.h")
target_link_libraries(3dmppc_editor_imgui PUBLIC SDL3::SDL3)

# --- the editor ---------------------------------------------------------------
file(GLOB_RECURSE RV_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(3dmppc-editor ${RV_EDITOR_SOURCES})
target_include_directories(3dmppc-editor PRIVATE src)
target_link_libraries(3dmppc-editor PRIVATE 3dmppc_editor_imgui)
# pdklib: the bitmap font now, the manifest parser later - the same data and
# code the discs and the burner use, never a copy of it.
target_link_libraries(3dmppc-editor PRIVATE 3dmppc_pdklib)

if(MSVC)
  target_compile_options(3dmppc-editor PRIVATE /W4)
else()
  target_compile_options(3dmppc-editor PRIVATE -Wall -Wextra)
endif()
