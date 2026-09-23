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

add_executable(3dmppc-editor src/main.cpp)

if(MSVC)
  target_compile_options(3dmppc-editor PRIVATE /W4)
else()
  target_compile_options(3dmppc-editor PRIVATE -Wall -Wextra)
endif()
