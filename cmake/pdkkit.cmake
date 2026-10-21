include_guard(GLOBAL)

# --- pdkkit -------------------------------------------------------------------
# 3dmppc_pdkkit is the development kit's headers: ECS, scene, components and the
# game frame. It is an INTERFACE target over pdk/kit/include, and it sits between
# pdklib and its users: pdk <- pdklib <- pdkkit <- {a disc built with [build] sdk = ["kit"], the editor}.
#
# Only the editor includes this file. The console, the authoring tools and the
# burner do not see pdkkit, so their include paths stay as they were.
get_filename_component(RV_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
include(${RV_REPO_ROOT}/cmake/pdklib.cmake)

add_library(3dmppc_pdkkit INTERFACE)
target_include_directories(3dmppc_pdkkit INTERFACE ${RV_REPO_ROOT}/pdk/kit/include)
target_link_libraries(3dmppc_pdkkit INTERFACE 3dmppc_pdklib)
