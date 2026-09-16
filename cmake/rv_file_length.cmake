# The 512-line rule, enforced by the build instead of by memory.
#
# pdk/README.md ("File conventions") says a source file stays under 512 lines,
# and the reason is not tidiness: a file over that length has almost always
# stopped doing one job, and a reader looking for one of the several jobs inside
# it has to walk past the others. The rule was written down and then broken four
# times in one branch, which is what a convention nobody checks does.
#
# clang-format cannot express this and neither can clang-tidy: one formats lines
# and the other reasons about the AST, and neither has an opinion about how long
# a file is. clang-tidy DOES bound a FUNCTION (readability-function-size, see
# .clang-tidy) - the two checks together are the pair, one per axis.
#
# C++ ONLY. A .lua chunk, a manifest, a README or a generated file is not
# subject: the convention is about the trees a C++ reader navigates.
#
# Escape hatch: -DRV_ALLOW_LONG_FILES=ON reports the offenders as a warning
# instead of failing. It exists for the middle of a refactor, when a file is
# briefly long on the way to being split, and not as a way to keep it.

set(RV_FILE_LENGTH_MAX 512 CACHE STRING "Maximum lines in a C++ source file")
option(RV_ALLOW_LONG_FILES "Warn instead of failing when a C++ file is too long" OFF)

function(rv_check_file_lengths)
    file(GLOB_RECURSE RV_LENGTH_CANDIDATES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.hpp
        ${CMAKE_SOURCE_DIR}/pdk/include/*.h
        ${CMAKE_SOURCE_DIR}/pdk/lib/*.cpp
        ${CMAKE_SOURCE_DIR}/pdk/lib/*.hpp
        ${CMAKE_SOURCE_DIR}/pdk/tools/*.cpp
        ${CMAKE_SOURCE_DIR}/pdk/tools/*.hpp
        ${CMAKE_SOURCE_DIR}/mppcdiscs/*.cpp
        ${CMAKE_SOURCE_DIR}/mppcdiscs/*.hpp)

    set(RV_TOO_LONG "")
    foreach(candidate ${RV_LENGTH_CANDIDATES})
        # Vendored code and build trees are nobody's convention to keep: a
        # dependency's file length is not ours to legislate.
        if(candidate MATCHES "/third_party/|/build/|/_deps/")
            continue()
        endif()
        file(STRINGS ${candidate} lines)
        list(LENGTH lines count)
        if(count GREATER RV_FILE_LENGTH_MAX)
            file(RELATIVE_PATH shown ${CMAKE_SOURCE_DIR} ${candidate})
            list(APPEND RV_TOO_LONG "  ${count} lines: ${shown}")
        endif()
    endforeach()

    if(RV_TOO_LONG)
        list(JOIN RV_TOO_LONG "\n" report)
        set(message
            "a C++ source file may not exceed ${RV_FILE_LENGTH_MAX} lines (pdk/README.md, File conventions):\n${report}\n"
            "Split it by the jobs it does. -DRV_ALLOW_LONG_FILES=ON downgrades this to a warning.")
        if(RV_ALLOW_LONG_FILES)
            message(WARNING ${message})
        else()
            message(FATAL_ERROR ${message})
        endif()
    endif()
endfunction()
