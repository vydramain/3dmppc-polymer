# mppcburner BURNS a disc: a prepared directory in, one .mppcdisc out. Named for
# the act, not the artifact — .mppcdisc is the disc, and a tool sharing that name
# would make every sentence in the documentation ambiguous.
#
# It compiles a game by GENERATING a CMake project and driving cmake/ninja, so
# cmake is a run-time dependency of the tool, not a build-time one. Writing our
# own build system to avoid that would trade a dependency everyone already has
# for a pile of code nobody wants to maintain.

# The sources are grouped the way the tree is: one directory per SUBCOMMAND the
# tool offers, one per PHASE of a burn, and one for what the phases share.
add_executable(mppcburner
  main.cpp

  # the two subcommands, named for what the user types
  rv_burner_build/rv_burner_build_runner.cpp
  rv_burner_build/rv_burner_target.cpp
  rv_burner_build/rv_burner_web.cpp
  rv_burner_build/rv_burner_build_assets.cpp
  rv_burner_build/rv_burner_bake_texture.cpp
  rv_burner_build/rv_burner_map.cpp
  rv_burner_build/rv_burner_kit.cpp
  rv_burner_inspect/rv_burner_inspect_runner.cpp

  # the setup subcommand: pinned tools installed once per user, not shipped in the package
  rv_burner_setup/rv_burner_setup.cpp

  # the sources check, run before [1/4]: mppcinspector on the disc's sources
  rv_burner_sources/rv_burner_sources_check.cpp

  # shared by every phase
  rv_burner_common/rv_burner_archive.cpp
  rv_burner_common/rv_burner_process.cpp

  # [2/4] compile
  rv_burner_compile/rv_burner_check.cpp
  rv_burner_compile/rv_burner_cmake.cpp
  rv_burner_compile/rv_burner_compile_sources.cpp

  # [3/4] assets
  rv_burner_assets/rv_burner_plan.cpp
  rv_burner_assets/rv_burner_baker_path.cpp
  rv_burner_assets/rv_burner_bake.cpp
  rv_burner_assets/rv_burner_bake_models.cpp
  rv_burner_assets/rv_burner_compile_scripts.cpp

  # [4/4] burn
  rv_burner_burn/rv_burner_burn.cpp

  # the .mppcdisc container itself, both sides: the burn phase writes it and the
  # inspect subcommand reads it, so it belongs to neither and sits beside both
  rv_burner_zip/rv_burner_zipwrite.cpp
  rv_burner_zip/rv_burner_zipread.cpp
)

# The tool root on the include path, so a file in one phase directory names a
# header in another the way the tree reads: "rv_burner_common/…". Headers inside
# one directory still include each other by bare name.
target_include_directories(mppcburner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The contract lives next door. The burner reads pdk/cv/rv_texture.h for the
# texel formats it enforces budgets against.
target_include_directories(mppcburner PRIVATE ${RV_REPO_ROOT}/pdk/include)

# pdklib: the output adapter (rv_stdio.hpp), the texel format names, and
# rv_mppctex.hpp — the one description of the container mppctexbaker writes and this
# tool reads back. Separate from the contract above because it is a separate tree.
#
# LINKED, not merely included: the manifest lexer/parser/semantic/binder are
# compiled once into 3dmppc_pdklib, and this tool consuming that object code is
# what keeps a second copy of the parser from existing here. The include path
# arrives with the target too; naming it above keeps the header dependency
# readable without chasing the target.
target_include_directories(mppcburner PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)
target_link_libraries(mppcburner PRIVATE 3dmppc_pdklib)

# LuaJIT, for compiling a disc's .lua into the .luac the archive carries. Linked
# rather than shelled out to, so the bytecode version is the one this repository
# pins instead of whatever `luajit` happens to be on the machine.
target_link_libraries(mppcburner PRIVATE libluajit)

# Stamps every log line with its origin; see pdklib/rv_logs/rv_logs.hpp.
target_compile_definitions(mppcburner PRIVATE RV_LOG_ORIGIN="tool")

# Copies the contract (pdk/include), pdklib (pdk/lib/include) and pdkkit (pdk/kit/include)
# into <bindir>/pdk, so the tools package is one portable directory; the burner will take
# its defaults from there.
# Runs on every build, not only on relink: a header change the burner does not include
# still changes the copy the discs are built against. The old copy is removed first, so
# a header deleted from pdk/ does not survive beside the binary.
add_custom_target(mppcburner_pdk ALL
  COMMENT "Replacing the PDK contract and pdklib headers in pdk/ beside mppcburner, which the discs are built against"
  COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:mppcburner>/pdk
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RV_REPO_ROOT}/pdk/include $<TARGET_FILE_DIR:mppcburner>/pdk/include
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RV_REPO_ROOT}/pdk/lib/include $<TARGET_FILE_DIR:mppcburner>/pdk/lib/include
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RV_REPO_ROOT}/pdk/kit/include $<TARGET_FILE_DIR:mppcburner>/pdk/kit/include
  VERBATIM
)
add_dependencies(mppcburner_pdk mppcburner)

if(MSVC)
  target_compile_options(mppcburner PRIVATE /W4)
else()
  target_compile_options(mppcburner PRIVATE -Wall -Wextra)
endif()
