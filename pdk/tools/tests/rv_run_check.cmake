# Runs one command and checks what it printed. Invoked by rv_pdktools_add_run_test through cmake -P.
#
#   -DRV_CHECK_COMMAND=<cmd;args>   command and arguments, a ;-list
#   -DRV_CHECK_EXIT=<code>          expected exit code, default 0
#   -DRV_CHECK_MATCH=<re;re>        every regex must match stdout+stderr
#   -DRV_CHECK_ABSENT=<re;re>       no regex may match
#   -DRV_CHECK_FIRST=<re>           the first stderr line must match (the progress protocol)
#   -DRV_CHECK_LAST=<re>            the last stderr line must match

if(NOT DEFINED RV_CHECK_EXIT)
  set(RV_CHECK_EXIT 0)
endif()

execute_process(COMMAND ${RV_CHECK_COMMAND}
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(all "${out}${err}")

if(NOT code EQUAL RV_CHECK_EXIT)
  message(FATAL_ERROR "exit code ${code}, expected ${RV_CHECK_EXIT}\n${all}")
endif()

foreach(re IN LISTS RV_CHECK_MATCH)
  if(NOT all MATCHES "${re}")
    message(FATAL_ERROR "output does not match '${re}'\n${all}")
  endif()
endforeach()
foreach(re IN LISTS RV_CHECK_ABSENT)
  if(all MATCHES "${re}")
    message(FATAL_ERROR "output matches '${re}', it must not\n${all}")
  endif()
endforeach()

# FIRST and LAST read stderr alone: stdout and stderr come back as separate strings, so their order is lost.
string(REGEX REPLACE "\n+$" "" trimmed "${err}")
string(REPLACE "\n" ";" lines "${trimmed}")
list(LENGTH lines count)
if(RV_CHECK_FIRST)
  list(GET lines 0 first)
  if(NOT first MATCHES "${RV_CHECK_FIRST}")
    message(FATAL_ERROR "first stderr line '${first}' does not match '${RV_CHECK_FIRST}'\n${all}")
  endif()
endif()
if(RV_CHECK_LAST)
  math(EXPR at "${count} - 1")
  list(GET lines ${at} last)
  if(NOT last MATCHES "${RV_CHECK_LAST}")
    message(FATAL_ERROR "last stderr line '${last}' does not match '${RV_CHECK_LAST}'\n${all}")
  endif()
endif()
