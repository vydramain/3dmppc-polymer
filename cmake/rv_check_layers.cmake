# The pdkkit layer boundary, enforced by the check-layers build target instead of at configure time.
#
# The stack is pdk <- pdklib <- pdkkit <- { a disc with [build] sdk = ["kit"], the editor,
# the mppcinspector and mppcrepairer tools }. The console, the other tools, pdklib and the discs
# without the kit must not see pdkkit: a disc that did not ask for the kit fails at build time;
# this check names the offending #include line without a build, so an upward include is caught
# where it is written.
#
# Limits: the sdk key is read line by line from the [build] section, so an array split over
# several lines is not seen (disc.toml keeps it on one line). A disc without disc.toml has no kit.
# Only #include lines are checked, and an include made through a macro (#include K) is not seen.
# C/C++ files only.

if(NOT RV_ROOT)
    message(FATAL_ERROR "RV_ROOT must be specified via -DRV_ROOT=<path>")
endif()

# Group 1 is the delimiter (quote or angle), group 2 the include path.
set(RV_LAYER_INCLUDE_RE "^[ \t]*#[ \t]*include[ \t]*([\"<])([^\">]*)[\">]")

# The two tools that link pdkkit by design; their directories are skipped whole.
# The rest of pdk/tools stays under CORE.
set(RV_LAYER_EXEMPT_RE "^pdk/tools/(mppcinspector|mppcrepairer)/")

set(RV_LAYER_RULE_CORE
    "src, pdk/tools (not mppcinspector, mppcrepairer), pdk/lib, pdk/include must not include pdkkit: pdkkit sits above pdklib")
set(RV_LAYER_RULE_KIT "pdk/kit quoted includes must start with pdk/, pdklib/ or pdkkit/: the kit sees only the layers below it")
set(RV_LAYER_RULE_DISC "a disc without [build] sdk = [\"kit\"] in disc.toml must not include pdkkit")

# Sets has_kit ON when the disc's [build] section names "kit" in its sdk key.
function(rv_layer_disc_has_kit disc has_kit)
    set(${has_kit} OFF PARENT_SCOPE)
    set(toml ${RV_ROOT}/mppcdiscs/${disc}/disc.toml)
    if(NOT EXISTS ${toml})
        return()
    endif()
    file(STRINGS ${toml} lines)
    set(in_build OFF)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line MATCHES "^\\[")
            set(in_build OFF)
            if(line MATCHES "^\\[build\\]")
                set(in_build ON)
            endif()
        elseif(in_build AND line MATCHES "^sdk[ \t]*=")
            string(REGEX REPLACE "#.*$" "" value "${line}")
            if(value MATCHES "\"kit\"")
                set(${has_kit} ON PARENT_SCOPE)
            endif()
        endif()
    endforeach()
endfunction()

function(rv_check_layers)
    file(GLOB_RECURSE RV_LAYER_CANDIDATES ${RV_ROOT}/src/* ${RV_ROOT}/pdk/* ${RV_ROOT}/mppcdiscs/*)

    set(RV_LAYER_REPORT "")
    foreach(candidate IN LISTS RV_LAYER_CANDIDATES)
        if(NOT candidate MATCHES "\\.(c|cc|cpp|h|hh|hpp)$")
            continue()
        endif()
        file(RELATIVE_PATH rel ${RV_ROOT} ${candidate})
        if(rel MATCHES "(^|/)(third_party|build|_deps|\\.mppcburn)/")
            continue()
        endif()

        if(rel MATCHES "${RV_LAYER_EXEMPT_RE}")
            continue()
        endif()

        set(rule "")
        if(rel MATCHES "^(src|pdk/tools|pdk/lib|pdk/include)/")
            set(rule CORE)
        elseif(rel MATCHES "^pdk/kit/")
            set(rule KIT)
        elseif(rel MATCHES "^mppcdiscs/")
            string(REGEX REPLACE "^mppcdiscs/([^/]*).*$" "\\1" disc "${rel}")
            rv_layer_disc_has_kit(${disc} has_kit)
            if(NOT has_kit)
                set(rule DISC)
            endif()
        endif()
        if(rule STREQUAL "")
            continue()
        endif()

        file(STRINGS ${candidate} lines)
        set(n 0)
        foreach(line IN LISTS lines)
            math(EXPR n "${n} + 1")
            if(NOT line MATCHES "${RV_LAYER_INCLUDE_RE}")
                continue()
            endif()
            set(delim "${CMAKE_MATCH_1}")
            set(path "${CMAKE_MATCH_2}")
            set(bad FALSE)
            if(rule STREQUAL "KIT")
                if(delim STREQUAL "\"" AND NOT path MATCHES "^(pdk|pdklib|pdkkit)/")
                    set(bad TRUE)
                endif()
            elseif(path MATCHES "(^|/)pdkkit/")
                set(bad TRUE)
            endif()
            if(bad)
                string(STRIP "${line}" shown)
                string(APPEND RV_LAYER_REPORT "  ${rel}:${n}: ${shown} - ${RV_LAYER_RULE_${rule}}\n")
            endif()
        endforeach()
    endforeach()

    if(RV_LAYER_REPORT)
        message(FATAL_ERROR "pdkkit layer boundary crossed (cmake/rv_check_layers.cmake):\n${RV_LAYER_REPORT}")
    endif()
    message(STATUS "check-layers: ok")
endfunction()

rv_check_layers()
