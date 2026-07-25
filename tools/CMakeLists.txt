# ─── NEUROSLOP ────────────────────────────────────────────────────────────────
# Сгенерировано Claude (claude-opus-5). Не проверено человеком.
# Ревизия: stage 9 — сборка офлайн-инструментов девкита (mppctex).
# ──────────────────────────────────────────────────────────────────────────────
#
# Host-side developer tools. Nothing here is part of the console or of a disc:
# these run on the developer's machine while a game is built. Consequently they
# link NOTHING from src/ and see no console header — the only shared truth is
# the format documented in pdk/cv/rv_texture.hpp, restated in tools/README.md.
#
# The file also stands alone, so the tools can be configured without the console:
#   cmake -S tools -B build-tools && cmake --build build-tools

cmake_minimum_required(VERSION 3.24)

# Guard so this works both as add_subdirectory(tools) and as a standalone -S tools.
if(NOT DEFINED PROJECT_NAME)
  project(3dmppc_tools LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- stb_image (header only, TOOL-ONLY dependency) ----------------------------
# The console decodes no image format and never will; stb lives here, downloaded
# into this tool's own build directory, so it cannot drift into src/ by accident.
# Pinned to a tag rather than master: a build that silently changes its decoder
# is a build that silently changes shipped assets.
set(MPPCTEX_STB_DIR ${CMAKE_CURRENT_BINARY_DIR}/_deps/stb)
if(NOT EXISTS ${MPPCTEX_STB_DIR}/stb_image.h)
  message(STATUS "mppctex: downloading stb_image.h")
  file(DOWNLOAD
    https://raw.githubusercontent.com/nothings/stb/f0569113c93ad095470c54bf34a17b36646bbbb5/stb_image.h
    ${MPPCTEX_STB_DIR}/stb_image.h
    TLS_VERIFY ON
    STATUS MPPCTEX_STB_STATUS
  )
  list(GET MPPCTEX_STB_STATUS 0 MPPCTEX_STB_CODE)
  if(NOT MPPCTEX_STB_CODE EQUAL 0)
    list(GET MPPCTEX_STB_STATUS 1 MPPCTEX_STB_MESSAGE)
    file(REMOVE ${MPPCTEX_STB_DIR}/stb_image.h)
    message(FATAL_ERROR "mppctex: cannot download stb_image.h: ${MPPCTEX_STB_MESSAGE}")
  endif()
endif()

# --- mppctex: PNG -> .mppctex -------------------------------------------------

add_executable(mppctex ${CMAKE_CURRENT_SOURCE_DIR}/mppctex.cpp)

# SYSTEM: stb_image.h is third-party and not warning-clean under -Wextra. Its
# implementation is textually part of mppctex.cpp, but the diagnostics still
# point INTO the header, so a system include directory silences exactly stb's
# noise and leaves every warning about our own code standing.
target_include_directories(mppctex SYSTEM PRIVATE ${MPPCTEX_STB_DIR})

if(MSVC)
  target_compile_options(mppctex PRIVATE /W4)
else()
  target_compile_options(mppctex PRIVATE -Wall -Wextra)
endif()
