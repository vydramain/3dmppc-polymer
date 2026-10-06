# 3dmppc-editor: the desktop application a game is authored in. A separate
# product from the console and the tools: it drives them as processes and links
# none of their code.
#
#   cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
#
# A development build of the console (-D3DMPPC_DEVTOOLS=ON) builds it as well.

cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(3dmppc_editor LANGUAGES C CXX)

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

# --- libvterm (vendored) -----------------------------------------------------
# third_party/libvterm/ORIGIN.md. The Terminal tile's screen: C99, compiled the
# way its own Makefile does, its warnings kept out of the editor's.
set(RV_VTERM_DIR ${RV_REPO_ROOT}/third_party/libvterm)
file(GLOB RV_VTERM_SOURCES CONFIGURE_DEPENDS ${RV_VTERM_DIR}/src/*.c)
add_library(3dmppc_editor_vterm STATIC ${RV_VTERM_SOURCES})
set_target_properties(3dmppc_editor_vterm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(3dmppc_editor_vterm SYSTEM PUBLIC ${RV_VTERM_DIR}/include)

# --- the editor ---------------------------------------------------------------
file(GLOB_RECURSE RV_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(3dmppc-editor ${RV_EDITOR_SOURCES})
target_include_directories(3dmppc-editor PRIVATE src)
target_link_libraries(3dmppc-editor PRIVATE 3dmppc_editor_imgui)
# libvterm: the Terminal tile's screen.
target_link_libraries(3dmppc-editor PRIVATE 3dmppc_editor_vterm)
# pdklib: the manifest parser the discs and the burner use, never a copy of it.
target_link_libraries(3dmppc-editor PRIVATE 3dmppc_pdklib)

# stb_image (vendored, third_party/stb) decodes the icons. The icons are read
# from the repository at run time, the way mppcburner finds pdk/.
target_include_directories(3dmppc-editor SYSTEM PRIVATE ${RV_REPO_ROOT}/third_party/stb)
target_compile_definitions(3dmppc-editor PRIVATE RV_EDITOR_ICON_DIR="${RV_REPO_ROOT}/third_party/win55-icons"
  RV_EDITOR_TEMPLATE_DIR="${RV_REPO_ROOT}/mppcdiscs")

# The code font, PxPlus IBM VGA 9x16 (third_party), read at run time the same way.
target_compile_definitions(3dmppc-editor PRIVATE
  RV_EDITOR_CODE_FONT_FILE="${RV_REPO_ROOT}/third_party/pxplus-ibm-vga/PxPlus_IBM_VGA_9x16.ttf"
  RV_EDITOR_CODE_FONT_SMALL_FILE="${RV_REPO_ROOT}/third_party/pxplus-ibm-vga/PxPlus_IBM_EGA_8x14.ttf")

# The code editor's nvim config (editor/nvim), read at run time the same way.
target_compile_definitions(3dmppc-editor PRIVATE
  RV_EDITOR_NVIM_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/nvim/rv_editor_init.lua")

# --- Catppuccin Mocha palette (third_party/catppuccin-palette/palette.json) ----
set(RV_PALETTE_JSON "${RV_REPO_ROOT}/third_party/catppuccin-palette/palette.json")
set(RV_PALETTE_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RV_PALETTE_HPP "${RV_PALETTE_GEN_DIR}/rv_editor_catppuccin_mocha.hpp")
set(RV_PALETTE_LUA "${RV_PALETTE_GEN_DIR}/rv_editor_catppuccin_mocha.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RV_PALETTE_JSON}")

file(READ "${RV_PALETTE_JSON}" RV_PALETTE_CONTENT)
string(JSON RV_MOCHA_COLORS GET "${RV_PALETTE_CONTENT}" "mocha" "colors")
string(JSON RV_MOCHA_LENGTH LENGTH "${RV_MOCHA_COLORS}")

if(RV_MOCHA_LENGTH LESS_EQUAL 0)
  message(FATAL_ERROR "Catppuccin Mocha palette is empty")
endif()

math(EXPR RV_MOCHA_LAST_INDEX "${RV_MOCHA_LENGTH} - 1")

# Get all colour names in JSON order.
set(RV_MOCHA_NAMES)
foreach(i RANGE 0 ${RV_MOCHA_LAST_INDEX})
  string(JSON RV_NAME MEMBER "${RV_MOCHA_COLORS}" ${i})
  list(APPEND RV_MOCHA_NAMES "${RV_NAME}")
endforeach()

# Generate C++ header and Lua table.
set(RV_CPP_LINES "// Generated from third_party/catppuccin-palette/palette.json by editor/CMakeLists.txt\n#pragma once\n#include <cstdint>\n\nnamespace rv_editor\n{\n")
set(RV_LUA_LINES "-- Generated from third_party/catppuccin-palette/palette.json by editor/CMakeLists.txt\nreturn {\n")

foreach(name IN LISTS RV_MOCHA_NAMES)
  string(JSON hex GET "${RV_MOCHA_COLORS}" "${name}" "hex")
  # Convert #rrggbb to 0xrrggbb (as uint32_t)
  string(REPLACE "#" "" hex_digits "${hex}")
  string(TOLOWER "${hex_digits}" hex_lower)
  set(cpp_var "rv_editor_mocha_${name}")
  string(APPEND RV_CPP_LINES "  inline constexpr uint32_t ${cpp_var} = 0x${hex_lower};\n")
  string(APPEND RV_LUA_LINES "  ${name} = \"${hex}\",\n")
endforeach()

string(APPEND RV_CPP_LINES "}\n")
string(APPEND RV_LUA_LINES "}\n")

file(CONFIGURE OUTPUT "${RV_PALETTE_HPP}" CONTENT "${RV_CPP_LINES}")
file(CONFIGURE OUTPUT "${RV_PALETTE_LUA}" CONTENT "${RV_LUA_LINES}")

target_include_directories(3dmppc-editor PRIVATE "${RV_PALETTE_GEN_DIR}")
target_compile_definitions(3dmppc-editor PRIVATE
  RV_EDITOR_NVIM_PALETTE="${RV_PALETTE_LUA}")

# --- Editor UI text strings (editor/texts/rv_editor_texts.toml) ---------------
# Embed the default text strings in the binary as a constexpr array. Read the TOML
# at configure time and generate a C++ header; changes trigger a rebuild.
set(RV_TEXTS_TOML "${CMAKE_CURRENT_SOURCE_DIR}/texts/rv_editor_texts.toml")
set(RV_TEXTS_HPP "${RV_PALETTE_GEN_DIR}/rv_editor_texts_default.hpp")

file(READ "${RV_TEXTS_TOML}" RV_TEXTS_CONTENT)

# Reject content that would break the raw string literal.
if(RV_TEXTS_CONTENT MATCHES "\\)rvtexts\"")
  message(FATAL_ERROR "rv_editor_texts.toml contains )rvtexts\", breaks C++ raw string literal")
endif()

set(RV_TEXTS_CPP "#pragma once\n")
string(APPEND RV_TEXTS_CPP "// Generated from editor/texts/rv_editor_texts.toml by editor/CMakeLists.txt\n")
string(APPEND RV_TEXTS_CPP "\n")
string(APPEND RV_TEXTS_CPP "namespace rv_editor\n")
string(APPEND RV_TEXTS_CPP "{\n")
string(APPEND RV_TEXTS_CPP "\n")
string(APPEND RV_TEXTS_CPP "inline constexpr char rv_editor_texts_default[] = R\"rvtexts(\n")
string(APPEND RV_TEXTS_CPP "${RV_TEXTS_CONTENT}")
string(APPEND RV_TEXTS_CPP ")rvtexts\";\n")
string(APPEND RV_TEXTS_CPP "\n")
string(APPEND RV_TEXTS_CPP "}\n")

# Write to .in file without substitution, then configure_file copies without substituting @ or ${.
file(WRITE "${RV_TEXTS_HPP}.in" "${RV_TEXTS_CPP}")
configure_file("${RV_TEXTS_HPP}.in" "${RV_TEXTS_HPP}" COPYONLY)

# nvim's channel is read on a thread of its own.
find_package(Threads REQUIRED)
target_link_libraries(3dmppc-editor PRIVATE Threads::Threads)

if(MSVC)
  target_compile_options(3dmppc-editor PRIVATE /W4)
else()
  target_compile_options(3dmppc-editor PRIVATE -Wall -Wextra)
endif()
