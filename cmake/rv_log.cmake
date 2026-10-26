# Log output of the cmake scripts (stage headers, step lines, log paths); include it or run it with -P.
# Run: cmake -DRV_LOG_CMD=<cmd;args> -DRV_LOG_FILE=<log> -DRV_LOG_BASE=<dir> -P cmake/rv_log.cmake
#      or cmake -DRV_LOG_STAGE=<stage> -P cmake/rv_log.cmake
# No cmake_minimum_required: included from CMakeLists, it would change the caller's policies.

# Lines of the log shown after a failure: enough for the error that ends a tool's output, few enough to read beside the rest.
set(RV_LOG_TAIL_LINES 40)

# At configure the header (no mode) and the steps (STATUS) may go to different streams; each message is flushed
# on write, so 2>&1 | tee keeps their order.
function(rv_log_header stage)
    message("")
    message("== ${stage} ==")
endfunction()

function(rv_log_step text)
    message(STATUS "${text}")
endfunction()

# Sets <out> to <path> relative to <base> when it lies inside <base>, else to its absolute path.
function(rv_log_relpath out path base)
    get_filename_component(path_abs "${path}" ABSOLUTE)
    get_filename_component(base_abs "${base}" ABSOLUTE)
    string(LENGTH "${base_abs}/" prefix_len)
    string(SUBSTRING "${path_abs}/" 0 ${prefix_len} head)
    if(head STREQUAL "${base_abs}/" AND NOT path_abs STREQUAL base_abs)
        string(SUBSTRING "${path_abs}" ${prefix_len} -1 rel)
    else()
        set(rel "${path_abs}")
    endif()
    set(${out} "${rel}" PARENT_SCOPE)
endfunction()

# Runs RV_LOG_CMD with stdout and stderr in RV_LOG_FILE. Success prints one line; failure prints the log tail and fails.
function(rv_log_script_run)
    execute_process(
        COMMAND ${RV_LOG_CMD}
        OUTPUT_FILE "${RV_LOG_FILE}"
        ERROR_FILE "${RV_LOG_FILE}"
        RESULT_VARIABLE rc)
    rv_log_relpath(log_rel "${RV_LOG_FILE}" "${RV_LOG_BASE}")
    if("${rc}" STREQUAL "0")
        message(STATUS "log: ${log_rel}")
        return()
    endif()
    # No list here: file(STRINGS) splits lines at ';' and escapes them. The text is matched whole instead: every line
    # ends with a newline (the last one gets it here), and the tail is the last N such lines after a newline.
    set(text "")
    if(EXISTS "${RV_LOG_FILE}")
        file(READ "${RV_LOG_FILE}" text)
    endif()
    if(NOT "${text}" STREQUAL "" AND NOT "${text}" MATCHES "\n$")
        string(APPEND text "\n")
    endif()
    string(REPEAT "[^\n]*\n" ${RV_LOG_TAIL_LINES} body)
    string(REGEX MATCH "\n${body}$" tail "\n${text}")
    if("${tail}" STREQUAL "")
        set(tail "${text}")
    else()
        string(SUBSTRING "${tail}" 1 -1 tail)
    endif()
    string(REGEX REPLACE "\n$" "" tail "${tail}")
    if(NOT "${tail}" STREQUAL "")
        message("${tail}")
    endif()
    # RESULT_VARIABLE is an exit code, or the reason the command did not run (or was signalled): not a number.
    if("${rc}" MATCHES "^[0-9]+$")
        set(why "exit ${rc}")
    else()
        set(why "${rc}")
    endif()
    message(FATAL_ERROR "command failed (${why}), the full log is ${log_rel}")
endfunction()

function(rv_log_script_main)
    if(NOT "${RV_LOG_STAGE}" STREQUAL "" AND NOT "${RV_LOG_CMD}" STREQUAL "")
        message(FATAL_ERROR "RV_LOG_CMD and RV_LOG_STAGE are exclusive: give one of them")
    endif()
    if(NOT "${RV_LOG_STAGE}" STREQUAL "")
        rv_log_header("${RV_LOG_STAGE}")
    elseif(NOT "${RV_LOG_CMD}" STREQUAL "")
        foreach(var RV_LOG_FILE RV_LOG_BASE)
            if("${${var}}" STREQUAL "")
                message(FATAL_ERROR "${var} must be specified via -D${var}=<value>")
            endif()
        endforeach()
        rv_log_script_run()
    else()
        message(FATAL_ERROR "RV_LOG_CMD or RV_LOG_STAGE must be specified via -D<name>=<value>")
    endif()
endfunction()

# Runs only when this file is the script (-P); an include from another script or CMakeLists does not.
if(CMAKE_SCRIPT_MODE_FILE AND "${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
    rv_log_script_main()
endif()
