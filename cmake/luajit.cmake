include_guard(GLOBAL)

include(FetchContent)

# --- LuaJIT --------------------------------------------------------------------
# TODO: Need to make fork with CMake build system only for my project for easiest
#       import to system
find_package(LuaJIT QUIET)
if(NOT LuaJIT_FOUND)
  message(STATUS "LuaJIT not found on system!")
  FetchContent_Declare(
    LuaJIT
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG v2.1
    GIT_SHALLOW TRUE
  )

  if(EXISTS ${FETCHCONTENT_BASE_DIR}/luajit-src/src/lua.hpp)
    set(LuaJIT_STATUS_MESSAGE "LuaJIT found here: ")
  else()
    message(STATUS "Fetching LuaJIT from source...")
    set(LuaJIT_STATUS_MESSAGE "LuaJIT successfully downloading to ")
  endif()

  FetchContent_MakeAvailable(LuaJIT)
  message(STATUS ${LuaJIT_STATUS_MESSAGE}${luajit_SOURCE_DIR})

endif()
# LuaJIT's Makefile has no out-of-tree build and writes objects into its source tree,
# which the dev build and the nested firmware build share. Each build works in a copy
# under its binary dir, made at configure time: a refetch of the source reconfigures.
# file(COPY) keeps timestamps, so make rebuilds only what changed. Objects, archives and
# executables left by an old in-tree make are not copied.
set(LUAJIT_BUILD_DIR ${CMAKE_BINARY_DIR}/luajit)
file(COPY ${luajit_SOURCE_DIR}/ DESTINATION ${LUAJIT_BUILD_DIR}
  PATTERN .git EXCLUDE
  PATTERN *.o EXCLUDE
  PATTERN *.a EXCLUDE
  PATTERN *.so* EXCLUDE
  PATTERN luajit EXCLUDE
  PATTERN buildvm EXCLUDE
  PATTERN minilua EXCLUDE
)
set(LUAJIT_MAKE_ARGS "")
if(CMAKE_SYSROOT)
  # Target objects get the sysroot and toolchain flags; buildvm runs on the build
  # machine, so HOST_CC is the same compiler without them.
  set(LUAJIT_SYSROOT_FLAGS "--sysroot=${CMAKE_SYSROOT} ${CMAKE_C_FLAGS}")
  set(LUAJIT_MAKE_ARGS
    CC=${CMAKE_C_COMPILER}
    HOST_CC=${CMAKE_C_COMPILER}
    "TARGET_CFLAGS=${LUAJIT_SYSROOT_FLAGS}"
  )
endif()
# A custom target has no outputs, so it runs on every build; make decides what to rebuild.
add_custom_target(libluajit_maker
  COMMAND make -C ${LUAJIT_BUILD_DIR}/src ${LUAJIT_MAKE_ARGS} libluajit.a
  BYPRODUCTS ${LUAJIT_BUILD_DIR}/src/libluajit.a
  VERBATIM
)
add_library(libluajit STATIC IMPORTED)
set_target_properties(libluajit PROPERTIES
  IMPORTED_LOCATION ${LUAJIT_BUILD_DIR}/src/libluajit.a
  INTERFACE_INCLUDE_DIRECTORIES ${LUAJIT_BUILD_DIR}/src
)
add_dependencies(libluajit libluajit_maker)
