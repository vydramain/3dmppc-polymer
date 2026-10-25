# Builds the player delivery tree <tree> (pconsole/ in the build): <tree>/<name> (launch file), bin/<name> (the console
# ELF), SteamLinuxRuntime_4/ (a copy without var/), LICENSES/LuaJIT.txt and VERSIONS.txt. Run by the player-tree target,
# which the ordinary build puts in ALL, so each build updates the tree in place: bin/ is never cleared because the
# player's saves and modes.toml live there. A file removed from the build stays until deleted by hand: the launch file
# in the tree root, and files in bin/ and LICENSES/. Files the runtime dropped go when its version changes (whole replacement).
# The runtime copy is replaced only when its VERSIONS.txt differs from the source's.
# No project license is shipped: the repository has none.
#
# Run: cmake -DRV_PLAYER_ELF=<ELF> -DRV_PLAYER_LAUNCH=<rv_launch.sh> -DRV_PLAYER_RUNTIME=<SteamLinuxRuntime_4 dir>
#            -DRV_PLAYER_LUAJIT_COPYRIGHT=<file> -DRV_PLAYER_LUAJIT_COMMIT=<commit> -DRV_PLAYER_VER_MAJOR=<n>
#            -DRV_PLAYER_VER_MINOR=<n> -DRV_PLAYER_TREE=<dir> -P <this file>

cmake_minimum_required(VERSION 3.24)

foreach(var RV_PLAYER_ELF RV_PLAYER_LAUNCH RV_PLAYER_RUNTIME RV_PLAYER_LUAJIT_COPYRIGHT RV_PLAYER_LUAJIT_COMMIT
            RV_PLAYER_VER_MAJOR RV_PLAYER_VER_MINOR RV_PLAYER_TREE)
    # A value of 0 is false in if(), so the test is STREQUAL "".
    if("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be specified via -D${var}=<value>")
    endif()
endforeach()

# The launch file runs bin/<name>, so the console and the launch copy both take the ELF's file name.
get_filename_component(RV_PLAYER_NAME "${RV_PLAYER_ELF}" NAME)
set(RV_PLAYER_EXEC_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Checked before any write: a wrong path must not touch the tree the player runs from.
foreach(var RV_PLAYER_ELF RV_PLAYER_LAUNCH RV_PLAYER_LUAJIT_COPYRIGHT)
    if(NOT EXISTS "${${var}}" OR IS_DIRECTORY "${${var}}")
        message(FATAL_ERROR "${var} is not an existing file: ${${var}}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${RV_PLAYER_RUNTIME}")
    message(FATAL_ERROR "RV_PLAYER_RUNTIME is not an existing directory: ${RV_PLAYER_RUNTIME}")
endif()

# The runtime's own depot version, e.g. 4.0.20261021.265997. Its VERSIONS.txt is also the stamp of the runtime copy.
message(STATUS "Reading the runtime version from ${RV_PLAYER_RUNTIME}/VERSIONS.txt")
file(READ "${RV_PLAYER_RUNTIME}/VERSIONS.txt" RV_PLAYER_RUNTIME_VERSIONS)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" RV_PLAYER_RUNTIME_VER "${RV_PLAYER_RUNTIME_VERSIONS}")
if("${RV_PLAYER_RUNTIME_VER}" STREQUAL "")
    message(FATAL_ERROR "no depot version in ${RV_PLAYER_RUNTIME}/VERSIONS.txt")
endif()

file(MAKE_DIRECTORY "${RV_PLAYER_TREE}/bin" "${RV_PLAYER_TREE}/LICENSES")

# The runtime comes first: the ELF is checked against it. The copy is current when its VERSIONS.txt equals the
# source's; a missing or different copy is replaced whole.
set(RV_PLAYER_RUNTIME_COPY "${RV_PLAYER_TREE}/SteamLinuxRuntime_4")
set(RV_PLAYER_RUNTIME_COPY_VERSIONS "")
if(EXISTS "${RV_PLAYER_RUNTIME_COPY}/VERSIONS.txt")
    file(READ "${RV_PLAYER_RUNTIME_COPY}/VERSIONS.txt" RV_PLAYER_RUNTIME_COPY_VERSIONS)
endif()
if("${RV_PLAYER_RUNTIME_COPY_VERSIONS}" STREQUAL "${RV_PLAYER_RUNTIME_VERSIONS}")
    message(STATUS "Steam Linux Runtime 4 ${RV_PLAYER_RUNTIME_VER} is already in ${RV_PLAYER_RUNTIME_COPY}: not copied")
else()
    message(STATUS "Copying Steam Linux Runtime 4 ${RV_PLAYER_RUNTIME_VER} to ${RV_PLAYER_RUNTIME_COPY}: "
                   "the container that runs the console")
    # The copy is built beside the old one and renamed into place only when complete, so an interrupted copy
    # never leaves a partial tree that carries the current stamp.
    set(RV_PLAYER_RUNTIME_PART "${RV_PLAYER_RUNTIME_COPY}.part")
    file(REMOVE_RECURSE "${RV_PLAYER_RUNTIME_PART}")
    # file(COPY) keeps the runtime's symlinks and permissions; the tree is a copy, never a link into the build dir.
    # The runtime's var/ is pressure-vessel's run state (thousands of files, absolute links into the builder's
    # ~/.local/share/Steam), written when the runtime was run from the build tree; it is not shipped.
    # The path goes into a regex: a trailing slash is stripped and metacharacters escaped so the match is literal.
    string(REGEX REPLACE "/+$" "" RV_PLAYER_RUNTIME_DIR "${RV_PLAYER_RUNTIME}")
    string(REGEX REPLACE "([][+.*?^$()|{}\\\\])" "\\\\\\1" RV_PLAYER_RUNTIME_RE "${RV_PLAYER_RUNTIME_DIR}")
    file(COPY "${RV_PLAYER_RUNTIME_DIR}/" DESTINATION "${RV_PLAYER_RUNTIME_PART}"
         REGEX "^${RV_PLAYER_RUNTIME_RE}/var$" EXCLUDE)
    # The old copy goes only after the new one is complete.
    file(REMOVE_RECURSE "${RV_PLAYER_RUNTIME_COPY}")
    file(RENAME "${RV_PLAYER_RUNTIME_PART}" "${RV_PLAYER_RUNTIME_COPY}")
endif()

# The ELF is copied beside bin/<name> and checked there, so a failing build never replaces the console the player
# runs. The check reads the tree's runtime copy, not the build's link.
set(RV_PLAYER_ELF_PART "${RV_PLAYER_TREE}/bin/${RV_PLAYER_NAME}.part")
message(STATUS "Checking bin/${RV_PLAYER_NAME} against the tree's runtime: a failure stops the run")
file(COPY_FILE "${RV_PLAYER_ELF}" "${RV_PLAYER_ELF_PART}")
file(CHMOD "${RV_PLAYER_ELF_PART}" PERMISSIONS ${RV_PLAYER_EXEC_PERMISSIONS})
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DRV_COMPAT_FILES=${RV_PLAYER_ELF_PART}"
            "-DRV_COMPAT_RUNTIME=${RV_PLAYER_RUNTIME_COPY}"
            -P "${CMAKE_CURRENT_LIST_DIR}/rv_linux_compat_check.cmake"
    RESULT_VARIABLE RV_PLAYER_COMPAT_RESULT
)
if(NOT RV_PLAYER_COMPAT_RESULT EQUAL 0)
    file(REMOVE "${RV_PLAYER_ELF_PART}")
    message(FATAL_ERROR "bin/${RV_PLAYER_NAME} breaks the Linux compatibility contract; see the check above. "
            "An earlier bin/${RV_PLAYER_NAME}, if any, is kept.")
endif()

# rename replaces the file in one step: a running console keeps its old inode, so there is no ETXTBSY.
message(STATUS "Installing bin/${RV_PLAYER_NAME} and the launch file to ${RV_PLAYER_TREE}")
file(RENAME "${RV_PLAYER_ELF_PART}" "${RV_PLAYER_TREE}/bin/${RV_PLAYER_NAME}")
# bin/ is never cleared: the console writes memcard.mppccard and modes.toml beside its ELF, and none is shipped.
file(COPY_FILE "${RV_PLAYER_LAUNCH}" "${RV_PLAYER_TREE}/${RV_PLAYER_NAME}")
file(CHMOD "${RV_PLAYER_TREE}/${RV_PLAYER_NAME}" PERMISSIONS ${RV_PLAYER_EXEC_PERMISSIONS})

message(STATUS "Writing LICENSES/LuaJIT.txt and VERSIONS.txt to ${RV_PLAYER_TREE}")
file(COPY_FILE "${RV_PLAYER_LUAJIT_COPYRIGHT}" "${RV_PLAYER_TREE}/LICENSES/LuaJIT.txt")
file(WRITE "${RV_PLAYER_TREE}/VERSIONS.txt"
    "3dmppc ${RV_PLAYER_VER_MAJOR}.${RV_PLAYER_VER_MINOR}\n"
    "Steam Linux Runtime 4 ${RV_PLAYER_RUNTIME_VER}\n"
    "LuaJIT ${RV_PLAYER_LUAJIT_COMMIT}\n")
