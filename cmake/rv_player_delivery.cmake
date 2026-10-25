# Builds the player delivery tree: <tree>/<name> (launch file), bin/<name> (the console ELF), SteamLinuxRuntime_4/,
# LICENSES/ and VERSIONS.txt; with a zip path, also the zip of the tree. Run by the player-tree and package-player targets.
# No project license is shipped: the repository has none.
#
# Run: cmake -DRV_PLAYER_ELF=<ELF> -DRV_PLAYER_LAUNCH=<rv_launch.sh> -DRV_PLAYER_RUNTIME=<SteamLinuxRuntime_4 dir>
#            -DRV_PLAYER_LUAJIT_COPYRIGHT=<file> -DRV_PLAYER_LUAJIT_COMMIT=<commit> -DRV_PLAYER_VER_MAJOR=<n>
#            -DRV_PLAYER_VER_MINOR=<n> -DRV_PLAYER_TREE=<dir> [-DRV_PLAYER_ZIP=<zip>] -P <this file>

cmake_minimum_required(VERSION 3.24)

foreach(var RV_PLAYER_ELF RV_PLAYER_LAUNCH RV_PLAYER_RUNTIME RV_PLAYER_LUAJIT_COPYRIGHT RV_PLAYER_LUAJIT_COMMIT
            RV_PLAYER_VER_MAJOR RV_PLAYER_VER_MINOR RV_PLAYER_TREE)
    # A value of 0 is false in if(), so the test is STREQUAL "".
    if("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be specified via -D${var}=<value>")
    endif()
endforeach()

# The optional zip is found before the tree is built: a missing zip fails the run without a half-made package.
if(RV_PLAYER_ZIP)
    find_program(RV_PLAYER_ZIP_PROGRAM zip)
    if(NOT RV_PLAYER_ZIP_PROGRAM)
        message(FATAL_ERROR "zip is not found: install zip to package ${RV_PLAYER_ZIP}")
    endif()
endif()

# The launch file runs bin/<name>, so the console and the launch copy both take the ELF's file name.
get_filename_component(RV_PLAYER_NAME "${RV_PLAYER_ELF}" NAME)
get_filename_component(RV_PLAYER_ROOT_NAME "${RV_PLAYER_TREE}" NAME)
get_filename_component(RV_PLAYER_PARENT "${RV_PLAYER_TREE}" DIRECTORY)
set(RV_PLAYER_EXEC_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Checked before the removal below: a wrong path must not cost the previous good tree.
foreach(var RV_PLAYER_ELF RV_PLAYER_LAUNCH RV_PLAYER_LUAJIT_COPYRIGHT)
    if(NOT EXISTS "${${var}}" OR IS_DIRECTORY "${${var}}")
        message(FATAL_ERROR "${var} is not an existing file: ${${var}}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${RV_PLAYER_RUNTIME}")
    message(FATAL_ERROR "RV_PLAYER_RUNTIME is not an existing directory: ${RV_PLAYER_RUNTIME}")
endif()

# Rebuilt from scratch every run: a file left by an earlier tree or zip must not ship.
message(STATUS "Removing the previous tree: ${RV_PLAYER_TREE}")
file(REMOVE_RECURSE "${RV_PLAYER_TREE}")
if(RV_PLAYER_ZIP)
    message(STATUS "Removing the previous zip: ${RV_PLAYER_ZIP}")
    file(REMOVE "${RV_PLAYER_ZIP}")
endif()
file(MAKE_DIRECTORY "${RV_PLAYER_TREE}/bin" "${RV_PLAYER_TREE}/LICENSES")

message(STATUS "Copying the console to ${RV_PLAYER_TREE}/bin and the launch file to ${RV_PLAYER_TREE}")
# bin/ is left writable, with no memcard or modes.toml shipped: the console writes them beside its ELF.
file(COPY_FILE "${RV_PLAYER_ELF}" "${RV_PLAYER_TREE}/bin/${RV_PLAYER_NAME}")
file(CHMOD "${RV_PLAYER_TREE}/bin/${RV_PLAYER_NAME}" PERMISSIONS ${RV_PLAYER_EXEC_PERMISSIONS})
file(COPY_FILE "${RV_PLAYER_LAUNCH}" "${RV_PLAYER_TREE}/${RV_PLAYER_NAME}")
file(CHMOD "${RV_PLAYER_TREE}/${RV_PLAYER_NAME}" PERMISSIONS ${RV_PLAYER_EXEC_PERMISSIONS})

message(STATUS "Copying Steam Linux Runtime 4 to ${RV_PLAYER_TREE}/SteamLinuxRuntime_4: the container that runs the console")
# file(COPY) keeps the runtime's symlinks and permissions; the tree is a copy, never a link into the build dir.
# The runtime's var/ is pressure-vessel's run state (thousands of files, absolute links into the builder's
# ~/.local/share/Steam), written when the runtime was run from the build tree; it is not shipped.
# The path goes into a regex: a trailing slash is stripped and metacharacters escaped so the match is literal.
string(REGEX REPLACE "/+$" "" RV_PLAYER_RUNTIME_DIR "${RV_PLAYER_RUNTIME}")
string(REGEX REPLACE "([][+.*?^$()|{}\\\\])" "\\\\\\1" RV_PLAYER_RUNTIME_RE "${RV_PLAYER_RUNTIME_DIR}")
file(COPY "${RV_PLAYER_RUNTIME_DIR}/" DESTINATION "${RV_PLAYER_TREE}/SteamLinuxRuntime_4"
     REGEX "^${RV_PLAYER_RUNTIME_RE}/var$" EXCLUDE)

# The runtime's own depot version, e.g. 4.0.20261021.265997.
message(STATUS "Reading the runtime version from ${RV_PLAYER_RUNTIME}/VERSIONS.txt")
file(READ "${RV_PLAYER_RUNTIME}/VERSIONS.txt" RV_PLAYER_RUNTIME_VERSIONS)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" RV_PLAYER_RUNTIME_VER "${RV_PLAYER_RUNTIME_VERSIONS}")
if("${RV_PLAYER_RUNTIME_VER}" STREQUAL "")
    message(FATAL_ERROR "no depot version in ${RV_PLAYER_RUNTIME}/VERSIONS.txt")
endif()

message(STATUS "Writing LICENSES/LuaJIT.txt and VERSIONS.txt to ${RV_PLAYER_TREE}")
file(COPY_FILE "${RV_PLAYER_LUAJIT_COPYRIGHT}" "${RV_PLAYER_TREE}/LICENSES/LuaJIT.txt")
file(WRITE "${RV_PLAYER_TREE}/VERSIONS.txt"
    "3dmppc ${RV_PLAYER_VER_MAJOR}.${RV_PLAYER_VER_MINOR}\n"
    "Steam Linux Runtime 4 ${RV_PLAYER_RUNTIME_VER}\n"
    "LuaJIT ${RV_PLAYER_LUAJIT_COMMIT}\n")

# The ELF must need only what this tree's own runtime defines; the check reads the copy, not the build's link.
message(STATUS "Checking bin/${RV_PLAYER_NAME} against the tree's runtime: a failure stops the run before zip")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DRV_COMPAT_FILES=${RV_PLAYER_TREE}/bin/${RV_PLAYER_NAME}"
            "-DRV_COMPAT_RUNTIME=${RV_PLAYER_TREE}/SteamLinuxRuntime_4"
            -P "${CMAKE_CURRENT_LIST_DIR}/rv_linux_compat_check.cmake"
    RESULT_VARIABLE RV_PLAYER_COMPAT_RESULT
)
if(NOT RV_PLAYER_COMPAT_RESULT EQUAL 0)
    message(FATAL_ERROR "bin/${RV_PLAYER_NAME} breaks the Linux compatibility contract; see the check above")
endif()

if(RV_PLAYER_ZIP)
    message(STATUS "Zipping ${RV_PLAYER_TREE} to ${RV_PLAYER_ZIP}")
    # Run from the parent so the archive's top dir is the root's name; -y keeps the runtime's symlinks.
    execute_process(
        COMMAND "${RV_PLAYER_ZIP_PROGRAM}" -r -y -q "${RV_PLAYER_ZIP}" "${RV_PLAYER_ROOT_NAME}"
        WORKING_DIRECTORY "${RV_PLAYER_PARENT}"
        RESULT_VARIABLE RV_PLAYER_ZIP_RESULT
    )
    if(NOT RV_PLAYER_ZIP_RESULT EQUAL 0)
        message(FATAL_ERROR "zip failed on ${RV_PLAYER_TREE}")
    endif()
endif()
