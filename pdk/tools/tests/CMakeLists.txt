# The PDK tools' tests: unit tests of the shared headers in 3dmppc-pdktools-tests, and a helper the tool
# subtasks use to run a built tool and check what it printed.

add_executable(3dmppc-pdktools-tests
  rv_pdktools_tests_main.cpp
  rv_pdktools_test_progress.cpp
  rv_pdktools_test_burner_steps.cpp
)
# The burner's step lists, a header with no sources behind it.
target_include_directories(3dmppc-pdktools-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../mppcburner)
# The binary stays in this build dir, never in the bin/ the superbuild delivers, even when it sets
# CMAKE_RUNTIME_OUTPUT_DIRECTORY.
set_target_properties(3dmppc-pdktools-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(3dmppc-pdktools-tests PRIVATE 3dmppc_pdklib)
if(MSVC)
  target_compile_options(3dmppc-pdktools-tests PRIVATE /W4)
else()
  target_compile_options(3dmppc-pdktools-tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME pdktools_unit COMMAND 3dmppc-pdktools-tests)

# rv_pdktools_add_run_test(NAME <name> COMMAND <tool> [args...]
#                          [EXIT <code>] [MATCH <re>...] [ABSENT <re>...] [FIRST <re>] [LAST <re>])
# Registers a ctest that runs COMMAND (a target name is replaced by its file) and checks the exit code
# (default 0) and the merged stdout+stderr: every MATCH regex found, no ABSENT regex found. FIRST and LAST
# check the stderr lines only: the first stderr line must match FIRST, the last must match LAST.
function(rv_pdktools_add_run_test)
  cmake_parse_arguments(RV "" "NAME;EXIT;FIRST;LAST" "COMMAND;MATCH;ABSENT" ${ARGN})
  set(command "")
  foreach(part IN LISTS RV_COMMAND)
    if(TARGET ${part})
      list(APPEND command "$<TARGET_FILE:${part}>")
    else()
      list(APPEND command "${part}")
    endif()
  endforeach()
  # add_test would split a -D value at its semicolons; $<SEMICOLON> reaches rv_run_check.cmake as the list separator.
  list(JOIN command "$<SEMICOLON>" command)
  set(defs "-DRV_CHECK_COMMAND=${command}")
  if(DEFINED RV_EXIT)
    list(APPEND defs "-DRV_CHECK_EXIT=${RV_EXIT}")
  endif()
  if(RV_MATCH)
    list(JOIN RV_MATCH "$<SEMICOLON>" match)
    list(APPEND defs "-DRV_CHECK_MATCH=${match}")
  endif()
  if(RV_ABSENT)
    list(JOIN RV_ABSENT "$<SEMICOLON>" absent)
    list(APPEND defs "-DRV_CHECK_ABSENT=${absent}")
  endif()
  if(RV_FIRST)
    list(APPEND defs "-DRV_CHECK_FIRST=${RV_FIRST}")
  endif()
  if(RV_LAST)
    list(APPEND defs "-DRV_CHECK_LAST=${RV_LAST}")
  endif()
  add_test(NAME ${RV_NAME}
    COMMAND ${CMAKE_COMMAND} ${defs} -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/rv_run_check.cmake)
endfunction()

# mppcinspector's progress protocol: its findings and --report stay byte-identical to the golden files taken
# before it spoke the protocol.
foreach(disc IN ITEMS clean:0 broken:1)
  string(REPLACE ":" ";" parts ${disc})
  list(GET parts 0 name)
  list(GET parts 1 code)
  add_test(NAME inspector_protocol_${name}
    COMMAND ${CMAKE_COMMAND}
      -DRV_INSPECTOR=$<TARGET_FILE:mppcinspector>
      -DRV_DISC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inspector-${name}
      -DRV_REPORT=${CMAKE_CURRENT_BINARY_DIR}/inspector-${name}.report
      -DRV_GOLDEN_OUT=${CMAKE_CURRENT_SOURCE_DIR}/golden/inspector-${name}.out
      -DRV_GOLDEN_REPORT=${CMAKE_CURRENT_SOURCE_DIR}/golden/inspector-${name}.report
      -DRV_EXIT=${code}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/rv_inspector_check.cmake)
endforeach()

# A failed run (no such disc directory) is still a protocol run: the plan first, one failed end line last.
add_test(NAME inspector_protocol_failed
  COMMAND ${CMAKE_COMMAND}
    -DRV_INSPECTOR=$<TARGET_FILE:mppcinspector>
    -DRV_DISC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/no-such-disc
    -DRV_REPORT=${CMAKE_CURRENT_BINARY_DIR}/inspector-failed.report
    -DRV_EXIT=2
    -P ${CMAKE_CURRENT_SOURCE_DIR}/rv_inspector_check.cmake)

# A finding line is never cut by the protocol: with stdout and stderr in one pipe (the burner captures the inspector so),
# the findings past the stdio buffer's size (the fixture has 120, over 9 KB) and the end line stay whole lines.
add_test(NAME inspector_findings_and_protocol_lines_stay_whole_in_one_pipe
  COMMAND ${CMAKE_COMMAND}
    -DRV_TOOL=$<TARGET_FILE:mppcinspector>
    -DRV_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inspector-many
    -DRV_EXIT=1
    -DRV_PROTOCOL=mppcinspector
    -P ${CMAKE_CURRENT_SOURCE_DIR}/rv_merged_check.cmake)

# --- The bakers speak the progress protocol (stderr, after the summary line on stdout). ---
# Good input: plan first, steps 1..N in order, last line [end] ok, exit 0, summary line kept.
# Bad input: the old error line kept, last line [end] failed: <reason>, exit 1 as before.
# Outputs go to this build dir.
set(RV_BAKER_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bakers)
set(RV_BAKER_OUT ${CMAKE_CURRENT_BINARY_DIR}/bakers-out)
file(MAKE_DIRECTORY ${RV_BAKER_OUT})

# rv_bakers_add_protocol_tests(<tool> GOOD <file> BAD <file> OUT <name> [ARGS <a>...]
#                              SUMMARY <re> ERROR <re> STEPS <label>...)
# STEPS are the plan's labels in order; ctest names are bakers_<tool>_good and bakers_<tool>_bad.
function(rv_bakers_add_protocol_tests tool)
  cmake_parse_arguments(RV "" "GOOD;BAD;OUT;SUMMARY;ERROR" "ARGS;STEPS" ${ARGN})
  list(LENGTH RV_STEPS total)
  set(plan "${tool}: \\[plan\\]")
  set(steps "${plan}")
  set(n 0)
  foreach(label IN LISTS RV_STEPS)
    math(EXPR n "${n} + 1")
    string(APPEND plan " ${label}")
    string(APPEND steps ".*${tool}: \\[${n}/${total}\\] ${label}")
  endforeach()
  list(GET RV_STEPS 0 first_label)
  rv_pdktools_add_run_test(NAME bakers_${tool}_good
    COMMAND ${tool} ${RV_BAKER_FIXTURES}/${RV_GOOD} ${RV_BAKER_OUT}/${RV_OUT} ${RV_ARGS}
    EXIT 0
    MATCH "${plan}" "${steps}" "${RV_SUMMARY}"
    ABSENT "\\[[0-9]+/[0-9]+\\].*${tool}: \\[plan\\]" "\\[end\\] failed"
    LAST "^${tool}: \\[end\\] ok$")
  rv_pdktools_add_run_test(NAME bakers_${tool}_bad
    COMMAND ${tool} ${RV_BAKER_FIXTURES}/${RV_BAD} ${RV_BAKER_OUT}/bad-${RV_OUT} ${RV_ARGS}
    EXIT 1
    MATCH "${plan}" "${tool}: \\[1/${total}\\] ${first_label}" "${RV_ERROR}"
    ABSENT "\\[end\\] ok"
    LAST "^${tool}: \\[end\\] failed: .+")
endfunction()

rv_bakers_add_protocol_tests(mppcmodbaker GOOD tri.gltf BAD garbage.gltf OUT tri.mppcmod
  SUMMARY "mppcmodbaker: [^\n]*tri.gltf -> [^\n]*tri.mppcmod \\(1 parts, 3 vertices, 1 triangles"
  ERROR "mppcmodbaker: '[^\n]*garbage.gltf' cannot be read as glTF 2.0"
  STEPS read quantize write)
rv_bakers_add_protocol_tests(mppcmusbaker GOOD tiny.wav BAD garbage.wav OUT tiny.pcm
  SUMMARY "mppcmusbaker: [^\n]*tiny.wav -> [^\n]*tiny.pcm \\(64 samples, 128 bytes\\)"
  ERROR "mppcmusbaker: '[^\n]*garbage.wav' is not a RIFF/WAVE file"
  STEPS read write)
rv_bakers_add_protocol_tests(mppctexbaker GOOD tiny.png BAD garbage.png OUT tiny.mppctex ARGS --format direct15
  SUMMARY "mppctexbaker: [^\n]*tiny.png -> [^\n]*tiny.mppctex \\(2x2, direct15, 24 bytes\\)"
  ERROR "mppctexbaker: cannot read '[^\n]*garbage.png'"
  STEPS read encode write)

# --- running mppcburner on discs ---
#
# The burner finds the bakers and the inspector in one directory (--baker): the tools of this build, linked there.
# The discs are copied into the build dir, as a run leaves a .mppcburn project in the disc directory.
set(RV_TEST_TOOLS_DIR ${CMAKE_CURRENT_BINARY_DIR}/tools)
set(RV_TEST_DISCS_DIR ${CMAKE_CURRENT_BINARY_DIR}/discs)
set(RV_TEST_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/out)
add_custom_target(pdktools_test_tools ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RV_TEST_TOOLS_DIR} ${RV_TEST_OUT_DIR}
  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:mppcinspector> ${RV_TEST_TOOLS_DIR}/mppcinspector
  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:mppcmodbaker> ${RV_TEST_TOOLS_DIR}/mppcmodbaker
  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:mppcmusbaker> ${RV_TEST_TOOLS_DIR}/mppcmusbaker
  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:mppctexbaker> ${RV_TEST_TOOLS_DIR}/mppctexbaker
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${RV_TEST_DISCS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RV_REPO_ROOT}/mppcdiscs/example-lua-bare ${RV_TEST_DISCS_DIR}/example-lua-bare
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/broken-scene ${RV_TEST_DISCS_DIR}/broken-scene
  VERBATIM)
add_dependencies(pdktools_test_tools mppcinspector mppcmodbaker mppcmusbaker mppctexbaker)

# A step line of the burner's own: "mppcburner: [n/5] label". The regexes below read the merged output.
set(RV_BURNER_STEPS_5 "mppcburner: \\[1/5\\] sources.*mppcburner: \\[2/5\\] manifest.*mppcburner: \\[3/5\\] compile.*mppcburner: \\[4/5\\] assets.*mppcburner: \\[5/5\\] burn")

rv_pdktools_add_run_test(NAME burner_build_disc_prints_plan_steps_and_one_end
  COMMAND mppcburner build ${RV_TEST_DISCS_DIR}/example-lua-bare --target disc -o ${RV_TEST_OUT_DIR}/lua-bare.mppcdisc
    --baker ${RV_TEST_TOOLS_DIR}
  FIRST "^mppcburner: \\[plan\\] sources manifest compile assets burn$"
  LAST "^mppcburner: \\[end\\] ok$"
  MATCH "${RV_BURNER_STEPS_5}"
  ABSENT "\\[1/4\\]" "mppcburner: \\[plan\\].*mppcburner: \\[plan\\]" "mppcburner: \\[end\\].*mppcburner: \\[end\\]")
add_dependencies(pdktools_test_tools mppcburner)

# Refused at the sources step: the inspector's findings are printed inside step 1 (its own protocol lines come with its
# subtask); the exit code is the one the burner has always had for them (RV_ERR_INVAL, 255).
rv_pdktools_add_run_test(NAME burner_build_broken_scene_fails_at_sources_with_one_end
  COMMAND mppcburner build ${RV_TEST_DISCS_DIR}/broken-scene --target disc -o ${RV_TEST_OUT_DIR}/broken.mppcdisc
    --baker ${RV_TEST_TOOLS_DIR}
  EXIT 255
  FIRST "^mppcburner: \\[plan\\] sources manifest compile assets burn$"
  LAST "^mppcburner: \\[end\\] failed: .+"
  MATCH "mppcburner: \\[1/5\\] sources.*scene-parse.*mppcburner: \\[end\\] failed"
  ABSENT "mppcburner: \\[2/5\\]" "\\[1/4\\]" "mppcburner: \\[plan\\].*mppcburner: \\[plan\\]"
          "mppcburner: \\[end\\].*mppcburner: \\[end\\]")

# inspect reads the disc the build test above burns: that test sets up the fixture, inspect requires it.
set_tests_properties(burner_build_disc_prints_plan_steps_and_one_end PROPERTIES FIXTURES_SETUP lua_bare_mppcdisc)

rv_pdktools_add_run_test(NAME burner_inspect_prints_plan_step_and_one_end
  COMMAND mppcburner inspect ${RV_TEST_OUT_DIR}/lua-bare.mppcdisc
  FIRST "^mppcburner: \\[plan\\] inspect$"
  LAST "^mppcburner: \\[end\\] ok$"
  MATCH "mppcburner: \\[1/1\\] inspect")
set_tests_properties(burner_inspect_prints_plan_step_and_one_end PROPERTIES FIXTURES_REQUIRED lua_bare_mppcdisc)

# A missing file fails at the only step: the plan, one failed end line last; 251 is the exit code the burner gives it now.
rv_pdktools_add_run_test(NAME burner_inspect_missing_file_fails_with_one_end
  COMMAND mppcburner inspect ${RV_TEST_OUT_DIR}/no-such-file.mppcdisc
  EXIT 251
  FIRST "^mppcburner: \\[plan\\] inspect$"
  LAST "^mppcburner: \\[end\\] failed: .+"
  ABSENT "mppcburner: \\[end\\].*mppcburner: \\[end\\]")

# bake-texture bakes the one PNG of a tiny disc: the plan, the manifest step, the texture step, one end line.
rv_pdktools_add_run_test(NAME burner_bake_texture_prints_plan_steps_and_one_end
  COMMAND mppcburner bake-texture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bake-texture-disc tex/dot.png
    -o ${RV_TEST_OUT_DIR}/dot.mppctex --baker ${RV_TEST_TOOLS_DIR}
  FIRST "^mppcburner: \\[plan\\] manifest texture$"
  LAST "^mppcburner: \\[end\\] ok$"
  MATCH "mppcburner: \\[1/2\\] manifest" "mppcburner: \\[2/2\\] texture"
  ABSENT "mppcburner: \\[plan\\].*mppcburner: \\[plan\\]" "mppcburner: \\[end\\].*mppcburner: \\[end\\]")
