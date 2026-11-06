# Console UI texts (src/texts/rv_console_texts.toml) embedded in the console as a
# constexpr raw string. Same mechanism as the editor's texts (editor/CMakeLists.txt).
# Included after target 3dmppc is created; the generated header goes to the build tree.

set(RV_CONSOLE_TEXTS_TOML "${CMAKE_CURRENT_SOURCE_DIR}/src/texts/rv_console_texts.toml")
set(RV_CONSOLE_TEXTS_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
set(RV_CONSOLE_TEXTS_HPP "${RV_CONSOLE_TEXTS_GEN_DIR}/rv_console_texts_default.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RV_CONSOLE_TEXTS_TOML}")

file(READ "${RV_CONSOLE_TEXTS_TOML}" RV_CONSOLE_TEXTS_CONTENT)

# Reject content that would break the raw string literal.
if(RV_CONSOLE_TEXTS_CONTENT MATCHES "\\)rvtexts\"")
  message(FATAL_ERROR "rv_console_texts.toml contains )rvtexts\", breaks C++ raw string literal")
endif()

set(RV_CONSOLE_TEXTS_CPP "#pragma once\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "// Generated from src/texts/rv_console_texts.toml by cmake/rv_console_texts.cmake\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "namespace rv_3dmppc\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "{\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "inline constexpr char rv_console_texts_default[] = R\"rvtexts(\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "${RV_CONSOLE_TEXTS_CONTENT}")
string(APPEND RV_CONSOLE_TEXTS_CPP ")rvtexts\";\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "\n")
string(APPEND RV_CONSOLE_TEXTS_CPP "}\n")

# Write to .in without substitution, then configure_file copies it only when the content changed.
file(WRITE "${RV_CONSOLE_TEXTS_HPP}.in" "${RV_CONSOLE_TEXTS_CPP}")
configure_file("${RV_CONSOLE_TEXTS_HPP}.in" "${RV_CONSOLE_TEXTS_HPP}" COPYONLY)

target_include_directories(3dmppc PRIVATE "${RV_CONSOLE_TEXTS_GEN_DIR}")
