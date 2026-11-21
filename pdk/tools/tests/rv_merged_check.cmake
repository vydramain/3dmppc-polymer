# Runs a tool with stdout and stderr in ONE pipe, as the burner and the editor read it, and checks that no progress
# protocol line is cut into a finding line (a tool's buffered stdout must not be split by a stderr line).
#
#   -DRV_TOOL=<exe>  -DRV_ARGS=<arg;arg>  -DRV_EXIT=<code>  -DRV_PROTOCOL=<tool name>

execute_process(COMMAND ${RV_TOOL} ${RV_ARGS}
  RESULT_VARIABLE code OUTPUT_VARIABLE merged ERROR_VARIABLE merged)
if(NOT code EQUAL RV_EXIT)
  message(FATAL_ERROR "exit code ${code}, expected ${RV_EXIT}\n${merged}")
endif()

# A line is whole when it starts with the tool's name or has no protocol marker inside it.
string(REPLACE "\n" ";" lines "${merged}")
set(seen 0)
foreach(line IN LISTS lines)
  if(line MATCHES "${RV_PROTOCOL}: \\[")
    math(EXPR seen "${seen} + 1")
    if(NOT line MATCHES "^${RV_PROTOCOL}: \\[")
      message(FATAL_ERROR "a protocol line is inside another line:\n${line}")
    endif()
  endif()
endforeach()
if(seen EQUAL 0)
  message(FATAL_ERROR "no protocol line in the output:\n${merged}")
endif()
