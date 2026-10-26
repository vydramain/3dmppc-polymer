# Copies a disc template to RV_TEMPLATE_DST without its top-level export/ and .mppcburn/ (the game export and the
# mppcburner build tree, which the delivery does not carry). Same-named directories deeper in the template are copied.
# The copy is rebuilt whole, so a file removed from the template is removed from the copy.
# Symlinks are copied as symlinks. One that is absolute, or points outside the template or into an excluded directory,
# is an error: the delivery is moved to other machines, and such a link would be broken there.
# Usage: cmake -DRV_TEMPLATE_SRC=<template dir> -DRV_TEMPLATE_DST=<copy dir> -P cmake/rv_template_copy.cmake

if(NOT RV_TEMPLATE_SRC OR NOT RV_TEMPLATE_DST)
    message(FATAL_ERROR "RV_TEMPLATE_SRC and RV_TEMPLATE_DST must be set")
endif()
if(NOT IS_DIRECTORY "${RV_TEMPLATE_SRC}")
    message(FATAL_ERROR "disc template is not a directory: ${RV_TEMPLATE_SRC}")
endif()

# Without a trailing slash, so the escaped path below is the prefix of every entry's path.
get_filename_component(RV_TEMPLATE_SRC "${RV_TEMPLATE_SRC}" ABSOLUTE)
string(REGEX REPLACE "/+$" "" RV_TEMPLATE_SRC "${RV_TEMPLATE_SRC}")
# GLOB_RECURSE takes a pattern: the glob characters of the template path are escaped.
string(REPLACE "[" "[[]" RV_TEMPLATE_GLOB "${RV_TEMPLATE_SRC}")
string(REPLACE "*" "[*]" RV_TEMPLATE_GLOB "${RV_TEMPLATE_GLOB}")
string(REPLACE "?" "[?]" RV_TEMPLATE_GLOB "${RV_TEMPLATE_GLOB}")
# The regex matches full paths, so the template path is escaped. Escaping keeps the dots of names literal.
string(REGEX REPLACE "([][.+*?^$(){}|\\\\])" "\\\\\\1" RV_TEMPLATE_SRC_RE "${RV_TEMPLATE_SRC}")
# Top-level directories and everything under them are left out; a deeper export/ is part of the disc.
set(RV_TEMPLATE_EXCLUDE_RE "^${RV_TEMPLATE_SRC_RE}/(export|\\.mppcburn)(/|$)")

file(GLOB_RECURSE RV_TEMPLATE_ENTRIES LIST_DIRECTORIES true "${RV_TEMPLATE_GLOB}/*")
foreach(entry IN LISTS RV_TEMPLATE_ENTRIES)
    if(NOT IS_SYMLINK "${entry}" OR entry MATCHES "${RV_TEMPLATE_EXCLUDE_RE}")
        continue()
    endif()
    file(READ_SYMLINK "${entry}" link_target)
    if(IS_ABSOLUTE "${link_target}")
        message(FATAL_ERROR "template symlink has an absolute target: ${entry} -> ${link_target}")
    endif()
    get_filename_component(entry_dir "${entry}" DIRECTORY)
    get_filename_component(resolved "${entry_dir}/${link_target}" ABSOLUTE)
    if(NOT (resolved STREQUAL "${RV_TEMPLATE_SRC}" OR resolved MATCHES "^${RV_TEMPLATE_SRC_RE}/")
        OR resolved MATCHES "${RV_TEMPLATE_EXCLUDE_RE}")
        message(FATAL_ERROR "template symlink leaves the template or points into an excluded directory:"
            " ${entry} -> ${link_target}")
    endif()
endforeach()

file(REMOVE_RECURSE "${RV_TEMPLATE_DST}")
file(COPY "${RV_TEMPLATE_SRC}/" DESTINATION "${RV_TEMPLATE_DST}" REGEX "${RV_TEMPLATE_EXCLUDE_RE}" EXCLUDE)
