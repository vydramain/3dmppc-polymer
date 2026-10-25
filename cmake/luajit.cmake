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
  set(LUAJIT_SOURCE_STATE "pinned commit in ${FETCHCONTENT_BASE_DIR}/luajit-src")
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
set(LUAJIT_MAKE_CMD make -C ${LUAJIT_BUILD_DIR}/src ${LUAJIT_MAKE_ARGS} libluajit.a)
# rv_log.cmake takes the command as one -D value; the ';' separators are written as $<SEMICOLON> so VERBATIM passes them intact.
string(REPLACE ";" "$<SEMICOLON>" LUAJIT_MAKE_CMD_GEN "${LUAJIT_MAKE_CMD}")
set(LUAJIT_MAKE_LOG ${CMAKE_BINARY_DIR}/logs/luajit-make.log)
# Base for the paths the script prints: the user's build dir (the root's RV_LOG_BASE), else this build's.
if("${RV_LOG_BASE}" STREQUAL "")
  set(LUAJIT_LOG_BASE ${CMAKE_BINARY_DIR})
else()
  set(LUAJIT_LOG_BASE ${RV_LOG_BASE})
endif()
# A custom target has no outputs, so it runs on every build; make decides what to rebuild.
# make warns 'rolling release version': expected, the copy has no .git, so the version line has no release number;
# the pinned commit sets the sources.
add_custom_target(libluajit_maker
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/logs
  COMMAND ${CMAKE_COMMAND}
          -DRV_LOG_CMD=${LUAJIT_MAKE_CMD_GEN}
          -DRV_LOG_FILE=${LUAJIT_MAKE_LOG}
          -DRV_LOG_BASE=${LUAJIT_LOG_BASE}
          -P ${CMAKE_CURRENT_LIST_DIR}/rv_log.cmake
  BYPRODUCTS ${LUAJIT_BUILD_DIR}/src/libluajit.a
  COMMENT "Building libluajit.a with LuaJIT make"
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
