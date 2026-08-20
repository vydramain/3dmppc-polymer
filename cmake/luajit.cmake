include_guard(GLOBAL)

include(FetchContent)

# --- LuaJIT --------------------------------------------------------------------
# TODO: Need to make fork with CMake build system only for my project for easiest
#       import to system
find_package(LuaJIT QUIET)
if(NOT LuaJIT_FOUND)
  message(STATUS "LuaJIT not found on system - fetching from source.")
  FetchContent_Declare(
    LuaJIT
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG v2.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(LuaJIT)
endif()
add_custom_command(OUTPUT ${luajit_SOURCE_DIR}/src/libluajit.a COMMAND make WORKING_DIRECTORY ${luajit_SOURCE_DIR})
add_custom_target(libluajit_maker DEPENDS ${luajit_SOURCE_DIR}/src/libluajit.a)
add_library(libluajit STATIC IMPORTED)
set_target_properties(libluajit PROPERTIES IMPORTED_LOCATION ${luajit_SOURCE_DIR}/src/libluajit.a INTERFACE_INCLUDE_DIRECTORIES ${luajit_SOURCE_DIR}/src)
add_dependencies(libluajit libluajit_maker)
