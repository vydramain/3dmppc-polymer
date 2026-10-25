include_guard(GLOBAL)

include(FetchContent)

# --- LuaJIT --------------------------------------------------------------------
# v2.1 is a moving branch; a pinned commit fetches the same source everywhere.
set(RV_LUAJIT_URL https://github.com/LuaJIT/LuaJIT.git)
set(RV_LUAJIT_COMMIT a4c064539ea931274d407e1249b4bb575424a0f4)
FetchContent_Declare(
  LuaJIT
  GIT_REPOSITORY ${RV_LUAJIT_URL}
  GIT_TAG ${RV_LUAJIT_COMMIT}
)
# FetchContent takes the source from FETCHCONTENT_SOURCE_DIR_LUAJIT, else from luajit-src in its base dir.
if(FETCHCONTENT_SOURCE_DIR_LUAJIT)
  set(LUAJIT_SOURCE_STATE "source given: ${FETCHCONTENT_SOURCE_DIR_LUAJIT}, no clone")
else()
  string(CONCAT LUAJIT_SOURCE_STATE
    "pinned commit ${RV_LUAJIT_COMMIT} cloned from ${RV_LUAJIT_URL} into ${FETCHCONTENT_BASE_DIR}/luajit-src "
    "unless an earlier configure already cloned it"
  )
endif()
message(STATUS "LuaJIT, not from the system: ${LUAJIT_SOURCE_STATE}")
FetchContent_MakeAvailable(LuaJIT)
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
# LuaJIT's Makefile defaults to gcc; the product is built with clang (the Steam Runtime 4 SDK's clang, on PATH
# inside the build), and buildvm runs on the same machine, so HOST_CC is the same.
set(LUAJIT_MAKE_ARGS CC=clang HOST_CC=clang)
# A custom target has no outputs, so it runs on every build; make decides what to rebuild.
# LUAJIT_MAKER_COMMENT explains the make warning about the rolling release version.
string(CONCAT LUAJIT_MAKER_COMMENT
  "Building libluajit.a with LuaJIT make in ${LUAJIT_BUILD_DIR}; "
  "if make prints the 'rolling release version' warning, it is expected: "
  "the copy has no .git, so the LuaJIT version line has no rolling release number; the pinned commit sets the sources"
)
add_custom_target(libluajit_maker
  COMMAND make -C ${LUAJIT_BUILD_DIR}/src ${LUAJIT_MAKE_ARGS} libluajit.a
  BYPRODUCTS ${LUAJIT_BUILD_DIR}/src/libluajit.a
  COMMENT "${LUAJIT_MAKER_COMMENT}"
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
