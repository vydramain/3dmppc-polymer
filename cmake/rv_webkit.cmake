include_guard(GLOBAL)

# --- the web kit ----------------------------------------------------------------
# The kit is the browser firmware for a disc that mppcburner links in: the console's
# static libraries, the built-in disc rv_dmain, the page's pre-js, and kit.txt. The
# game disc is not in it; mppcburner links that in. No executable is built.
# Installed under CMAKE_INSTALL_PREFIX.

# The PDK version (RV_MPPC_VER_MAJOR/MINOR) comes from cmake/rv_dv_version.cmake; kit.txt keeps the old pdk_version otherwise.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)

# The SDL3 file is installed as is, so it must be a static library.
get_target_property(RV_KIT_SDL3_TYPE SDL3::SDL3 TYPE)
if(NOT RV_KIT_SDL3_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "The web kit needs SDL3::SDL3 as a static library, not ${RV_KIT_SDL3_TYPE}")
endif()

# The kit ships SDL3's license from the fetched source tree; a system SDL3 has no such tree
# here, so CMake does not know its license path.
if(NOT DEFINED sdl3_SOURCE_DIR)
  message(FATAL_ERROR "The web kit needs SDL3 fetched from source (sdl3_SOURCE_DIR), not a system install: "
    "a kit without its license is not released")
endif()

# Link order: the console, then the built-in disc rv_dmain, then the platform library, the
# pdklib it uses, and SDL3 that the platform library uses. The game disc is not in the
# kit; kit.txt link_group says how it joins the link.
install(TARGETS 3dmppc rv_dmain 3dmppc_platform_sdl3 3dmppc_pdklib ARCHIVE DESTINATION lib)
install(FILES $<TARGET_FILE:SDL3::SDL3> DESTINATION lib)
install(FILES ${RV_WEB_PRE_JS} DESTINATION .)
get_filename_component(RV_KIT_PRE_JS_NAME ${RV_WEB_PRE_JS} NAME)
set(RV_WEB_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/src/web/rv_web_shell.html)
install(FILES ${RV_WEB_SHELL} DESTINATION .)
get_filename_component(RV_KIT_SHELL_NAME ${RV_WEB_SHELL} NAME)

# Third-party licenses go to licenses/ in the kit, for whoever packages the page.
set(RV_KIT_LICENSES_DIR licenses)
set(RV_KIT_SDL3_LICENSE_NAME SDL3.txt)
install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt DESTINATION ${RV_KIT_LICENSES_DIR} RENAME ${RV_KIT_SDL3_LICENSE_NAME})

# kit.txt: "key = value" lines. Paths are relative to the kit directory. A CMake list would
# reach the burner as one word, so link_flags is written as one space-separated line.
string(REPLACE ";" " " RV_KIT_LINK_FLAGS "${RV_WEB_LINK_FLAGS}")
file(GENERATE
  OUTPUT ${CMAKE_BINARY_DIR}/web-kit/kit.txt
  CONTENT "# 3dmppc web kit. Written by the build; read by mppcburner.
# pdk_version: the PDK the console was built against; mppcburner refuses another.
# build_type: CMAKE_BUILD_TYPE of the console; mppcburner builds the disc with the same one.
# libs: static libraries in link order, relative to the kit directory.
# link_group: yes = the libs and the game disc's archive go between -Wl,--start-group and
#   -Wl,--end-group, so the disc's position among them does not matter. Without it the
#   disc must come after the console and before pdklib.
# link_flags: emcc flags of the console's link line, separated by spaces.
# pre_js: the --pre-js file of the console's link line.
# shell: the HTML page shell of the disc's index.html.
# licenses: license files of the third-party parts the kit ships, separated by spaces.
pdk_version = ${RV_MPPC_VER_MAJOR}.${RV_MPPC_VER_MINOR}
build_type = ${CMAKE_BUILD_TYPE}
libs = lib/$<TARGET_FILE_NAME:3dmppc> lib/$<TARGET_FILE_NAME:rv_dmain> lib/$<TARGET_FILE_NAME:3dmppc_platform_sdl3> lib/$<TARGET_FILE_NAME:3dmppc_pdklib> lib/$<TARGET_FILE_NAME:SDL3::SDL3>
link_group = yes
link_flags = ${RV_KIT_LINK_FLAGS}
pre_js = ${RV_KIT_PRE_JS_NAME}
shell = ${RV_KIT_SHELL_NAME}
licenses = ${RV_KIT_LICENSES_DIR}/${RV_KIT_SDL3_LICENSE_NAME}
"
)
install(FILES ${CMAKE_BINARY_DIR}/web-kit/kit.txt DESTINATION .)
