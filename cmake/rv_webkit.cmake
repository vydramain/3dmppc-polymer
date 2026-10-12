include_guard(GLOBAL)

# --- the web kit ----------------------------------------------------------------
# The kit is the browser firmware for a disc that mppcburner links in: the console's
# static libraries, the built-in disc rv_dmain, the page's pre-js, and kit.txt. The
# game disc is not in it; mppcburner links that in. No executable is built.
# Installed under CMAKE_INSTALL_PREFIX.

# The PDK version is read from the contract header, so the number lives in one place.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pdk/include/pdk/de/rv_dv.h RV_KIT_DV_HEADER)
string(REGEX MATCH "#define RV_MPPC_VER_MAJOR ([0-9]+)" _ "${RV_KIT_DV_HEADER}")
set(RV_KIT_VER_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define RV_MPPC_VER_MINOR ([0-9]+)" _ "${RV_KIT_DV_HEADER}")
set(RV_KIT_VER_MINOR "${CMAKE_MATCH_1}")
# "0" is false in CMake, so a minor version of 0 is checked against the empty string.
if(RV_KIT_VER_MAJOR STREQUAL "" OR RV_KIT_VER_MINOR STREQUAL "")
  message(FATAL_ERROR "The web kit cannot read RV_MPPC_VER_MAJOR/MINOR from pdk/include/pdk/de/rv_dv.h")
endif()

# The SDL3 file is installed as is, so it must be a static library.
get_target_property(RV_KIT_SDL3_TYPE SDL3::SDL3 TYPE)
if(NOT RV_KIT_SDL3_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "The web kit needs SDL3::SDL3 as a static library, not ${RV_KIT_SDL3_TYPE}")
endif()

# Link order: the console, then the built-in disc rv_dmain, then the platform library, the
# pdklib it uses, and SDL3 that the platform library uses. The game disc is not in the
# kit; kit.txt link_group says how it joins the link.
install(TARGETS 3dmppc rv_dmain 3dmppc_platform_sdl3 3dmppc_pdklib ARCHIVE DESTINATION lib)
install(FILES $<TARGET_FILE:SDL3::SDL3> DESTINATION lib)
install(FILES ${RV_WEB_PRE_JS} DESTINATION .)
get_filename_component(RV_KIT_PRE_JS_NAME ${RV_WEB_PRE_JS} NAME)

# kit.txt: "key = value" lines. Paths are relative to the kit directory.
file(GENERATE
  OUTPUT ${CMAKE_BINARY_DIR}/web-kit/kit.txt
  CONTENT "# 3dmppc web kit. Written by the build; read by mppcburner.
# pdk_version: the PDK the console was built against; mppcburner refuses another.
# build_type: CMAKE_BUILD_TYPE of the console; mppcburner builds the disc with the same one.
# libs: static libraries in link order, relative to the kit directory.
# link_group: yes = the libs and the game disc's archive go between -Wl,--start-group and
#   -Wl,--end-group, so the disc's position among them does not matter. Without it the
#   disc must come after the console and before pdklib.
# link_flags: emcc flags of the console's link line.
# pre_js: the --pre-js file of the console's link line.
pdk_version = ${RV_KIT_VER_MAJOR}.${RV_KIT_VER_MINOR}
build_type = ${CMAKE_BUILD_TYPE}
libs = lib/$<TARGET_FILE_NAME:3dmppc> lib/$<TARGET_FILE_NAME:rv_dmain> lib/$<TARGET_FILE_NAME:3dmppc_platform_sdl3> lib/$<TARGET_FILE_NAME:3dmppc_pdklib> lib/$<TARGET_FILE_NAME:SDL3::SDL3>
link_group = yes
link_flags = ${RV_WEB_LINK_FLAGS}
pre_js = ${RV_KIT_PRE_JS_NAME}
"
)
install(FILES ${CMAKE_BINARY_DIR}/web-kit/kit.txt DESTINATION .)
