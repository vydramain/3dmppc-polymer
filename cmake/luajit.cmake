include_guard(GLOBAL)

include(FetchContent)

# --- LuaJIT --------------------------------------------------------------------
# v2.1 is a moving branch; a pinned commit fetches the same source everywhere.
set(RV_LUAJIT_COMMIT a4c064539ea931274d407e1249b4bb575424a0f4)
FetchContent_Declare(
  LuaJIT
  GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
  GIT_TAG ${RV_LUAJIT_COMMIT}
)
FetchContent_MakeAvailable(LuaJIT)
message(STATUS "LuaJIT ${RV_LUAJIT_COMMIT} at ${luajit_SOURCE_DIR}")
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
# LuaJIT's Makefile defaults to gcc; the product is built with clang (LLVM 23.1.4 first on PATH
# in the Steam Runtime 4 SDK), and buildvm runs on the same machine, so HOST_CC is the same.
set(LUAJIT_MAKE_ARGS CC=clang HOST_CC=clang)
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
  # ffi.load calls dlopen/dlsym; before glibc 2.34 they are in libdl.
  INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}"
)
add_dependencies(libluajit libluajit_maker)
