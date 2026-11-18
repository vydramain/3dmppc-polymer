# Runs mppcinspector on a fixture disc with --report and checks the contract of the progress protocol and
# that the findings output and the report are unchanged.
#
#   -DRV_INSPECTOR=<exe>  -DRV_DISC=<dir>  -DRV_REPORT=<file to write>
#   -DRV_GOLDEN_OUT=<file>  -DRV_GOLDEN_REPORT=<file>  -DRV_EXIT=<0|1>
#
# -DRV_EXIT=2 (a missing disc) checks only the plan and the failed end line.
# The protocol goes to stderr and the findings to stdout, so they are captured apart: stderr must be exactly
# the protocol of a finished run, and stdout + stderr without the protocol lines must equal the golden file
# (taken from the inspector before it spoke the protocol) byte for byte, so must the report.

file(REMOVE "${RV_REPORT}")
execute_process(COMMAND ${RV_INSPECTOR} ${RV_DISC} --report ${RV_REPORT}
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL RV_EXIT)
  message(FATAL_ERROR "exit code ${code}, expected ${RV_EXIT}\n${out}${err}")
endif()

if(RV_EXIT EQUAL 2)
  # Failed run: the plan first and the failed end line, with the reason, last; steps and findings do not matter.
  string(REGEX REPLACE "\n+$" "" trimmed "${out}${err}")
  if(NOT trimmed MATCHES "^mppcinspector: \\[plan\\] disc scenes\n")
    message(FATAL_ERROR "the first line is not the plan:\n${out}${err}")
  endif()
  if(NOT trimmed MATCHES "\nmppcinspector: \\[end\\] failed: [^\n]*no disc directory[^\n]*$")
    message(FATAL_ERROR "the last line is not end failed with the reason:\n${out}${err}")
  endif()
  return()
endif()

# Exit 0 ends ok; exit 1 (error findings) ends failed with the error count, after both steps.
if(RV_EXIT EQUAL 0)
  set(end "ok")
else()
  set(end "failed: 1 error\\(s\\)")
endif()
set(shape "^mppcinspector: \\[plan\\] disc scenes\nmppcinspector: \\[1/2\\] disc [^\n]*\nmppcinspector: \\[2/2\\] scenes [^\n]*\nmppcinspector: \\[end\\] ${end}\n$")
if(NOT err MATCHES "${shape}")
  message(FATAL_ERROR "stderr is not plan, 1/2 disc, 2/2 scenes, end ${end}:\n${err}")
endif()

string(REGEX REPLACE "mppcinspector: \\[[^\n]*\n" "" plain "${out}${err}")
file(READ "${RV_GOLDEN_OUT}" golden_out)
if(NOT plain STREQUAL golden_out)
  message(FATAL_ERROR "output without protocol lines differs from ${RV_GOLDEN_OUT}\n--- got\n${plain}--- golden\n${golden_out}")
endif()

file(READ "${RV_REPORT}" report)
file(READ "${RV_GOLDEN_REPORT}" golden_report)
if(NOT report STREQUAL golden_report)
  message(FATAL_ERROR "report differs from ${RV_GOLDEN_REPORT}\n--- got\n${report}--- golden\n${golden_report}")
endif()
