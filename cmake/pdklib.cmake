include_guard(GLOBAL)

# --- the contract and pdklib --------------------------------------------------
# One definition of the two targets every project in this repository builds
# against. The console, the authoring tools and the editor include this file
# instead of each declaring its own copy.
#
# 3dmppc_pdk is the contract: an include directory and nothing else.
#
# 3dmppc_pdklib is the disc-side helper library written AGAINST the contract:
# mostly header-only math, camera, transform, text and diagnostics, plus the
# statically linked manifest parser built from pdk/lib/include/pdklib/rv_manifest/.
# It is not the contract (the console implements nothing here) and not a game,
# so it is its own tree.
#
# The repository root is found from this file, so the module means the same
# thing whichever project includes it.
get_filename_component(RV_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${RV_REPO_ROOT}/pdk/include)

file(GLOB_RECURSE 3DMPPC_PDKLIB_SOURCES CONFIGURE_DEPENDS
  ${RV_REPO_ROOT}/pdk/lib/include/pdklib/*.cpp
)
# The glTF reader needs cgltf, which the console does not have: mppcmodbaker compiles it itself.
list(FILTER 3DMPPC_PDKLIB_SOURCES EXCLUDE REGEX "/rv_models/rv_mppcmod_gltf")
add_library(3dmppc_pdklib STATIC ${3DMPPC_PDKLIB_SOURCES})
target_include_directories(3dmppc_pdklib PUBLIC ${RV_REPO_ROOT}/pdk/lib/include)
target_link_libraries(3dmppc_pdklib PUBLIC 3dmppc_pdk)
# RV_LOG writes __FILE__; the prefix map makes those paths relative to the repository root, so the build machine's path
# stays out of targets linked with 3dmppc_pdklib (console, editor, mppcburner); mppcburner's disc.so is not covered.
target_compile_options(3dmppc_pdklib PUBLIC
  $<$<NOT:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>>:-ffile-prefix-map=${RV_REPO_ROOT}/=>
)
