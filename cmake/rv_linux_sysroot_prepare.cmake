# Makes Valve's unpacked Steam Runtime 3 "sniper" SDK sysroot usable by the host clang:
# absolute symlinks become relative, gcc-14 gets the libgcc links that only gcc-10 ships,
# and a wayland-scanner wrapper runs the sysroot's own scanner.
# Usage: cmake -DRV_SYSROOT=<dir> -P cmake/rv_linux_sysroot_prepare.cmake
# Idempotent: the stamp is written last, so a run that finds it does nothing.

if(NOT RV_SYSROOT)
    message(FATAL_ERROR "RV_SYSROOT must be specified via -DRV_SYSROOT=<path>")
endif()
if(NOT IS_DIRECTORY "${RV_SYSROOT}")
    message(FATAL_ERROR "RV_SYSROOT is not a directory: ${RV_SYSROOT}")
endif()
get_filename_component(RV_SYSROOT "${RV_SYSROOT}" REALPATH)
get_filename_component(RV_SYSROOT_PARENT "${RV_SYSROOT}" DIRECTORY)

# The wrapper puts the sysroot path inside double quotes in a sh script; these characters would change its meaning.
foreach(ch IN ITEMS "$" "`" "\"" "\\")
    string(FIND "${RV_SYSROOT}" "${ch}" pos)
    if(NOT pos EQUAL -1)
        message(FATAL_ERROR "RV_SYSROOT may not contain '${ch}' (the wrapper quotes it in sh): ${RV_SYSROOT}")
    endif()
endforeach()

# Outside the sysroot: the sysroot is the SDK's runtime tree, and the wrapper is a host build tool that names
# the sysroot's absolute path. Keeping it out leaves the tree a plain copy of the SDK.
set(RV_SYSROOT_WRAPPER "${RV_SYSROOT_PARENT}/wayland-scanner")
set(RV_SYSROOT_STAMP "${RV_SYSROOT_PARENT}/sysroot-prepared.stamp")

# Replaces the symlink at <link> with one to <target>. The new link is made under a temporary name in the same
# directory and renamed over the old one, so a failed create leaves the old link where it was.
function(rv_replace_link link target)
    set(temp "${link}.rv-tmp")
    file(REMOVE "${temp}")
    file(CREATE_LINK "${target}" "${temp}" RESULT result SYMBOLIC)
    if(NOT "${result}" STREQUAL "0")
        file(REMOVE "${temp}")
        message(FATAL_ERROR "cannot create link ${temp} -> ${target}: ${result}")
    endif()
    file(RENAME "${temp}" "${link}" RESULT result)
    if(NOT "${result}" STREQUAL "0")
        file(REMOVE "${temp}")
        message(FATAL_ERROR "cannot replace ${link} with a link to ${target}: ${result}")
    endif()
endfunction()

# A symlink to an absolute path resolves on the host, so ld would load /lib64/ld-linux-x86-64.so.2 from the host.
# Such a link is rewritten relative, resolved inside the sysroot. A link whose target is absent keeps its absolute
# target and is counted as dangling. EXISTS follows a directory link that stays absolute through the host, so a
# chain through one is not detected; it resolves via the host, like the runtime leftovers.
# Links are listed by find, which does not follow symlinks.
function(rv_relativize_absolute_links)
    execute_process(COMMAND find "${RV_SYSROOT}" -type l OUTPUT_VARIABLE listing RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "cannot list symlinks in ${RV_SYSROOT}: find exited ${rc}")
    endif()
    # A "[" in a name swallows the rest of a CMake list and a ";" splits the name, so such a link is refused.
    string(REGEX MATCH "[^\n]*\\[[^\n]*" offender "${listing}")
    if(NOT "${offender}" STREQUAL "")
        message(FATAL_ERROR "symlink with '[' in its path cannot be processed: ${offender}")
    endif()
    string(REGEX MATCH "[^\n]*;[^\n]*" offender "${listing}")
    if(NOT "${offender}" STREQUAL "")
        message(FATAL_ERROR "symlink with ';' in its path cannot be processed: ${offender}")
    endif()
    string(STRIP "${listing}" listing)
    string(REPLACE "\n" ";" links "${listing}")

    # A target that is itself an absolute link not yet rewritten resolves on the host, so passes repeat until
    # nothing changes; what is left is dangling.
    set(pending "${links}")
    set(converted_links "")
    set(converted_targets "")
    set(progress TRUE)
    while(progress AND pending)
        set(progress FALSE)
        set(still "")
        foreach(link IN LISTS pending)
            file(READ_SYMLINK "${link}" target)
            if(NOT IS_ABSOLUTE "${target}")
                continue()
            endif()
            set(inside "${RV_SYSROOT}${target}")
            if(NOT EXISTS "${inside}")
                list(APPEND still "${link}")
                continue()
            endif()
            get_filename_component(link_dir "${link}" DIRECTORY)
            cmake_path(RELATIVE_PATH inside BASE_DIRECTORY "${link_dir}" OUTPUT_VARIABLE relative)
            rv_replace_link("${link}" "${relative}")
            list(APPEND converted_links "${link}")
            list(APPEND converted_targets "${target}")
            set(progress TRUE)
        endforeach()
        set(pending "${still}")
    endwhile()
    set(dangling_links "${pending}")

    # A later conversion can change what an earlier one resolves to, so every converted link is checked again.
    # A converted link that no longer resolves is restored to its absolute target; that can break another, so
    # the check repeats until nothing is restored. A chain through a directory link that stays absolute is not
    # detected here (EXISTS resolves it via the host).
    set(restored TRUE)
    while(restored)
        set(restored FALSE)
        set(keep_links "")
        set(keep_targets "")
        list(LENGTH converted_links count)
        if(count GREATER 0)
            math(EXPR last "${count} - 1")
            foreach(i RANGE ${last})
                list(GET converted_links ${i} link)
                list(GET converted_targets ${i} target)
                file(READ_SYMLINK "${link}" relative)
                get_filename_component(link_dir "${link}" DIRECTORY)
                if(EXISTS "${link_dir}/${relative}")
                    list(APPEND keep_links "${link}")
                    list(APPEND keep_targets "${target}")
                else()
                    rv_replace_link("${link}" "${target}")
                    list(APPEND dangling_links "${link}")
                    set(restored TRUE)
                endif()
            endforeach()
        endif()
        set(converted_links "${keep_links}")
        set(converted_targets "${keep_targets}")
    endwhile()

    list(LENGTH converted_links converted)
    list(LENGTH dangling_links dangling)
    message(STATUS "absolute symlinks made relative: ${converted}; dangling and left unchanged: ${dangling}")
endfunction()

# gcc-14 in the SDK has no libgcc_s.so or libgcc_eh.a, which -static-libgcc and -lgcc_s need. gcc-10 has them, but its
# directory cannot go on -L: it would also supply its older libstdc++.a. So the two files are linked from gcc-10 alone.
function(rv_link_gcc10_runtime)
    set(gcc14 "${RV_SYSROOT}/usr/lib/gcc-14/lib/gcc/x86_64-linux-gnu/14")
    set(gcc10 "${RV_SYSROOT}/usr/lib/gcc/x86_64-linux-gnu/10")
    if(NOT IS_DIRECTORY "${gcc14}")
        message(FATAL_ERROR "missing gcc-14 runtime directory: ${gcc14}")
    endif()
    foreach(name IN ITEMS libgcc_s.so libgcc_eh.a)
        if(NOT EXISTS "${gcc10}/${name}")
            message(FATAL_ERROR "missing ${gcc10}/${name}: gcc-14 in the SDK needs it from gcc-10")
        endif()
        if(EXISTS "${gcc14}/${name}" AND NOT IS_SYMLINK "${gcc14}/${name}")
            message(FATAL_ERROR "${gcc14}/${name} is a real file, refusing to replace it with a link")
        endif()
        # Relative from gcc-14/14 up to usr/lib, so the link stays inside the sysroot.
        rv_replace_link("${gcc14}/${name}" "../../../../../gcc/x86_64-linux-gnu/10/${name}")
    endforeach()
endfunction()

# Protocol code must match the sysroot's libwayland (1.18); the host's scanner emits calls 1.18 lacks.
# The sysroot's loader and libraries are named explicitly, so the scanner needs no host library path.
function(rv_write_wayland_scanner_wrapper)
    file(GLOB loaders "${RV_SYSROOT}/lib/x86_64-linux-gnu/ld-*.so")
    list(LENGTH loaders loader_count)
    if(NOT loader_count EQUAL 1)
        message(FATAL_ERROR "expected one ld-*.so in ${RV_SYSROOT}/lib/x86_64-linux-gnu, found ${loader_count}")
    endif()
    set(scanner "${RV_SYSROOT}/usr/bin/wayland-scanner")
    if(NOT EXISTS "${scanner}")
        message(FATAL_ERROR "missing sysroot wayland-scanner: ${scanner}")
    endif()
    set(libs "${RV_SYSROOT}/lib/x86_64-linux-gnu:${RV_SYSROOT}/usr/lib/x86_64-linux-gnu")
    file(WRITE "${RV_SYSROOT_WRAPPER}"
        "#!/bin/sh\n"
        "# Runs the sysroot's wayland-scanner (libwayland 1.18) with the sysroot's loader and libraries.\n"
        "exec \"${loaders}\" --library-path \"${libs}\" \"${scanner}\" \"\$@\"\n")
    file(CHMOD "${RV_SYSROOT_WRAPPER}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endfunction()

if(EXISTS "${RV_SYSROOT_STAMP}")
    message(STATUS "sysroot already prepared (${RV_SYSROOT_STAMP}): nothing to do")
    return()
endif()

rv_relativize_absolute_links()
rv_link_gcc10_runtime()
rv_write_wayland_scanner_wrapper()

file(WRITE "${RV_SYSROOT_STAMP}" "prepared by cmake/rv_linux_sysroot_prepare.cmake\n")
message(STATUS "sysroot prepared: ${RV_SYSROOT}")
