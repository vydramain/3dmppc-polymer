# The PDK tools' tests: unit tests of the shared headers in 3dmppc-pdktools-tests, and a helper the tool
# subtasks use to run a built tool and check what it printed.

add_executable(3dmppc-pdktools-tests
  rv_pdktools_tests_main.cpp
  rv_pdktools_test_progress.cpp
)
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
# (default 0) and the merged stdout+stderr: every MATCH regex found, no ABSENT regex found, the first
# line matching FIRST, the last matching LAST.
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
