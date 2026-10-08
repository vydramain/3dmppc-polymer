# The 512-line rule, enforced by a build target instead of at configure time.
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

if(NOT RV_ROOT)
    message(FATAL_ERROR "RV_ROOT must be specified via -DRV_ROOT=<path>")
endif()
if(NOT RV_FILE_LENGTH_MAX)
    set(RV_FILE_LENGTH_MAX 512)
endif()

function(rv_check_file_lengths)
    file(GLOB_RECURSE RV_LENGTH_CANDIDATES
        ${RV_ROOT}/src/*.cpp
        ${RV_ROOT}/src/*.hpp
        ${RV_ROOT}/pdk/include/*.h
        ${RV_ROOT}/pdk/lib/*.cpp
        ${RV_ROOT}/pdk/lib/*.hpp
        ${RV_ROOT}/pdk/tools/*.cpp
        ${RV_ROOT}/pdk/tools/*.hpp
        ${RV_ROOT}/editor/*.cpp
        ${RV_ROOT}/editor/*.hpp
        ${RV_ROOT}/mppcdiscs/*.cpp
        ${RV_ROOT}/mppcdiscs/*.hpp)

    set(RV_TOO_LONG "")
    foreach(candidate ${RV_LENGTH_CANDIDATES})
        # Vendored code and build trees are nobody's convention to keep: a
        # dependency's file length is not ours to legislate.
        if(candidate MATCHES "/third_party/|/build/|/_deps/")
            continue()
        endif()
        # Count newline characters without converting the content to a CMake list.
        file(READ ${candidate} content)
        string(LENGTH "${content}" full)
        string(REPLACE "\n" "" stripped "${content}")
        string(LENGTH "${stripped}" rest)
        math(EXPR count "${full} - ${rest}")
        # A last line without a newline is still a line.
        string(REGEX MATCH "[^\n]$" unterminated "${content}")
        if(unterminated)
            math(EXPR count "${count} + 1")
        endif()
        if(count GREATER RV_FILE_LENGTH_MAX)
            file(RELATIVE_PATH shown ${RV_ROOT} ${candidate})
            list(APPEND RV_TOO_LONG "  ${count} lines: ${shown}")
        endif()
    endforeach()

    if(RV_TOO_LONG)
        list(JOIN RV_TOO_LONG "\n" report)
        message(FATAL_ERROR
            "a C++ source file may not exceed ${RV_FILE_LENGTH_MAX} lines (pdk/README.md, File conventions):\n${report}\n"
            "Split it by the jobs it does.")
    endif()
endfunction()

rv_check_file_lengths()
