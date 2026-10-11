# mppcmodbaker BAKES a model: glTF 2.0 (.glb) in, .mppcmod and its PNG textures out.
# The glTF reader is rv_modbaker_gltf.cpp in this directory; cgltf is vendored in third_party/cgltf.

add_executable(mppcmodbaker mppcmodbaker.cpp rv_modbaker_gltf.cpp rv_modbaker_accessor.cpp)

# SYSTEM: cgltf.h is third-party and not warning-clean under -Wextra; the diagnostics
# of our own code stay standing.
target_include_directories(mppcmodbaker SYSTEM PRIVATE ${RV_REPO_ROOT}/third_party/cgltf)

# The contract headers: the version comes from pdk/, the output adapter from pdklib/.
target_include_directories(mppcmodbaker PRIVATE ${RV_REPO_ROOT}/pdk/include)
target_include_directories(mppcmodbaker PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)

if(MSVC)
  target_compile_options(mppcmodbaker PRIVATE /W4)
else()
  target_compile_options(mppcmodbaker PRIVATE -Wall -Wextra)
endif()
