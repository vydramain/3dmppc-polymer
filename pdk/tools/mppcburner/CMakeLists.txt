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
  rv_burner_inspect/rv_burner_inspect_runner.cpp

  # shared by every phase
  rv_burner_common/rv_burner_archive.cpp
  rv_burner_common/rv_burner_globs.cpp
  rv_burner_common/rv_burner_process.cpp

  # [2/4] compile
  rv_burner_compile/rv_burner_check.cpp
  rv_burner_compile/rv_burner_cmake.cpp
  rv_burner_compile/rv_burner_compile_sources.cpp

  # [3/4] assets
  rv_burner_assets/rv_burner_plan.cpp
  rv_burner_assets/rv_burner_baker_path.cpp
  rv_burner_assets/rv_burner_bake.cpp
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

# The contract lives next door. The burner reads pdk/cv/rv_texture.hpp for the
# texel formats it enforces budgets against.
target_include_directories(mppcburner PRIVATE ${RV_REPO_ROOT}/pdk/include)

# pdklib: the output adapter (rv_stdio.hpp), the texel format names, and
# rv_mppctex.hpp — the one description of the container mppcbaker writes and this
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

# Where a GENERATED disc project should look for the contract and the disc-side
# SDK. Baked in so the tool works out of the box inside this repository, and
# overridable at run time (--pdk / --pdklib) so a disc directory living anywhere
# else builds just as well.
target_compile_definitions(mppcburner PRIVATE
  RV_BURNER_DEFAULT_PDK="${RV_REPO_ROOT}/pdk/include"
  RV_BURNER_DEFAULT_PDKLIB="${RV_REPO_ROOT}/pdk/lib/include"
)

if(MSVC)
  target_compile_options(mppcburner PRIVATE /W4)
else()
  target_compile_options(mppcburner PRIVATE -Wall -Wextra)
endif()
