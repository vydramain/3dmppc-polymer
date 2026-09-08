# rv_cdef_gen.cmake
#
# LuaJIT's ffi.cdef() takes a STRING of C declarations. It is not a
# preprocessor: it cannot follow #include and it cannot expand #define. So the
# console carries the PDK's declarations as string literals compiled into the
# executable, generated here from the real headers under pdk/include/pdk/.
#
# Every such header marks its declarable region with
# `/* RV_CDEF_BEGIN */` ... `/* RV_CDEF_END */`: everything the contract
# exposes is inside, everything a C++ compiler needs but ffi.cdef cannot eat
# (include guards, #include, extern "C") is outside.
#
# This file writes ${RV_CDEF_OUTPUT}, containing exactly two symbols:
#
#   extern const char rv_pdk_cdef[]   = "...";  // the sliced regions, glued
#   extern const char rv_pdk_consts[] = "...";  // a Lua table source
#
# rv_pdk_cdef is the concatenation of the marker-region text of every header,
# in DEPENDENCY order (a type must be declared before it is used). The
# RV_CDEF_ORDERED_HEADERS list below is the single place that order is
# decided - alphabetical order does not work (e.g. rv_ca.h uses rv_sample, so
# rv_sample.h must come first).
#
# rv_pdk_consts scrapes every `#define RV_<NAME> <value>` that sits OUTSIDE
# the marker regions (see pdk/README.md, "enum vs #define": these constants
# can never reach Lua through ffi.cdef, which is exactly why they need this
# second, separate channel), skipping include guards and any #define whose
# body is not a plain integer or a simple integer expression (an integer
# literal - decimal or hex, optionally wrapped in {U,}INT{8,16,32,64}_C(...),
# or a parenthesized `|` of already-defined RV_ constants of that shape).
# The leading "RV_" is dropped from the Lua key.
#
# --------------------------------------------------------------------------
# This file is used two ways:
#
#   - include()d from the top-level CMakeLists.txt at configure time: it only
#     defines RV_CDEF_ORDERED_HEADERS (so CMakeLists.txt can build a DEPENDS
#     list) and wires an add_custom_command that re-invokes THIS SAME file in
#     `cmake -P` script mode whenever a PDK header changes.
#   - run standalone (`cmake -DRV_PDK_INCLUDE_DIR=... -DRV_CDEF_OUTPUT=...
#     -P rv_cdef_gen.cmake`) at build time: it does the actual
#     reading/slicing/writing.
#
# CMAKE_SCRIPT_MODE_FILE is set only for the `cmake -P` case, so it is what
# tells the two uses apart.
# --------------------------------------------------------------------------

set(RV_CDEF_BEGIN_MARKER "/* RV_CDEF_BEGIN */")
set(RV_CDEF_END_MARKER "/* RV_CDEF_END */")

# The ordered header list: THE single place the concatenation order is
# decided. A header may only follow the headers whose declarations it uses.
# Paths are relative to pdk/include/pdk/.
set(RV_CDEF_ORDERED_HEADERS
  ca/rv_sample.h # rv_sample                                - no PDK type deps
  ca/rv_loop.h # rv_loop                                     - no PDK type deps
  ca/rv_voice_conf.h # rv_voice_conf.loop_type: enum rv_loop - after rv_loop.h
  ca/rv_ca.h # uses rv_sample*, rv_voice_conf*               - after both above
  cd/rv_cd.h # rv_cd                                         - no PDK type deps
  cio/rv_imouse.h # rv_imouse                                - no PDK type deps
  cio/rv_isource.h # rv_isource/rv_iaxes/rv_imotion/rv_istate - no PDK type deps
  cio/rv_ohaptic.h # rv_ohrumble/rv_ohpulse/rv_oheffect       - no PDK type deps
  cio/rv_cio.h # uses rv_istate, rv_imouse, rv_oheffect       - after the three above
  cl/rv_cl.h # rv_cl                                          - no PDK type deps
  cm/rv_cm.h # rv_cm                                          - no PDK type deps
  cv/rv_vertex.h # rv_color/rv_uv/rv_vertex                   - no PDK type deps
  cv/rv_texel.h # rv_color5                                   - no PDK type deps
  cv/rv_texture.h # rv_texfmt/rv_texture                      - no PDK type deps
  cv/rv_primitives.h # rv_polygon/rv_sprite use rv_vertex/rv_color - after rv_vertex.h
  cv/rv_cv.h # uses rv_vertex(rv_color), rv_texture, rv_primitive - after all three above
  de/rv_de.h # rv_de forward-declares its own rv_pdko         - no cross-header deps
  de/rv_dv.h # rv_mppc_disc_create_fn/destroy_fn use rv_de*   - after rv_de.h
  rv_err.h # rv_err                                           - no PDK type deps
  rv_pdko.h # uses rv_ca*, rv_cd*, rv_cm*, rv_cio*, rv_cv*, rv_cl* - after all six controllers
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
  # ======================= configure-time wiring =======================
  return()
endif()

# =========================== build-time: generate ===========================

if(NOT RV_PDK_INCLUDE_DIR)
  message(FATAL_ERROR "rv_cdef_gen: RV_PDK_INCLUDE_DIR is required")
endif()
if(NOT RV_CDEF_OUTPUT)
  message(FATAL_ERROR "rv_cdef_gen: RV_CDEF_OUTPUT is required")
endif()

set(RV_PDK_ROOT "${RV_PDK_INCLUDE_DIR}/pdk")

function(rv_escape_cxx_string_line IN_LINE OUT_VAR)
  string(REPLACE "\\" "\\\\" _escaped "${IN_LINE}")
  string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
  set(${OUT_VAR} "${_escaped}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Pass 1 - slice + concatenate the RV_CDEF_BEGIN/END regions, in dependency
# order, stripping "//" and "///" comment lines (blank lines are already
# dropped by file(STRINGS)).
# ---------------------------------------------------------------------------

# NOTE: this accumulates via string(APPEND), not list(APPEND). A source line
# routinely contains a literal ';' (every declaration ends in one), and a
# plain list(APPEND var "text-with-;") does NOT escape it for list storage -
# the ';' is then indistinguishable from a list separator and later
# iteration/joining silently splits the line apart. Plain string
# concatenation has no such gotcha.
set(RV_CDEF_BODY_TEXT "")

foreach(RELHDR ${RV_CDEF_ORDERED_HEADERS})
  set(HDR "${RV_PDK_ROOT}/${RELHDR}")
  if(NOT EXISTS "${HDR}")
    message(FATAL_ERROR "rv_cdef_gen: listed header not found: ${HDR}")
  endif()

  file(STRINGS "${HDR}" HDR_LINES ENCODING UTF-8)
  set(_in_region FALSE)
  set(_saw_region FALSE)

  foreach(LINE ${HDR_LINES})
    string(STRIP "${LINE}" _trimmed)

    if(_trimmed STREQUAL "${RV_CDEF_BEGIN_MARKER}")
      set(_in_region TRUE)
      set(_saw_region TRUE)
      continue()
    elseif(_trimmed STREQUAL "${RV_CDEF_END_MARKER}")
      set(_in_region FALSE)
      continue()
    endif()

    if(NOT _in_region)
      continue()
    endif()

    # a whole line that is nothing but a "//" or "///" comment - drop it.
    # (block comments and trailing same-line "//" comments are left alone:
    # ffi.cdef parses both just fine, and only whole comment lines were asked
    # to be stripped)
    if(_trimmed MATCHES "^//")
      continue()
    endif()

    rv_escape_cxx_string_line("${LINE}" _escaped)
    string(APPEND RV_CDEF_BODY_TEXT "    \"${_escaped}\\n\"\n")
  endforeach()

  if(NOT _saw_region)
    message(FATAL_ERROR "rv_cdef_gen: ${HDR} has no RV_CDEF_BEGIN/RV_CDEF_END markers")
  endif()
endforeach()

string(REGEX REPLACE "\n$" "" RV_CDEF_BODY_TEXT "${RV_CDEF_BODY_TEXT}")

# ---------------------------------------------------------------------------
# Pass 2 - scrape #define RV_<NAME> <value> constants that sit OUTSIDE the
# marker regions, across every header (not just the ones listed above: a
# header carrying no cdef region at all, like cv/rv_pipeline.h, can still
# carry constants).
# ---------------------------------------------------------------------------

file(GLOB_RECURSE RV_CONST_ALL_HEADERS RELATIVE "${RV_PDK_ROOT}" "${RV_PDK_ROOT}/*.h")
list(SORT RV_CONST_ALL_HEADERS)

set(RV_CONST_KEYS "") # full macro names ("RV_FOO") seen so far, resolved
set(RV_CONST_VALUES "") # their literal values (parallel to RV_CONST_KEYS)
set(RV_CONSTS_BODY_TEXT "") # "  \"NAME = value,\\n\"" lines, in encounter order

foreach(RELHDR ${RV_CONST_ALL_HEADERS})
  set(HDR "${RV_PDK_ROOT}/${RELHDR}")
  file(STRINGS "${HDR}" HDR_LINES ENCODING UTF-8)
  set(_in_region FALSE)

  foreach(LINE ${HDR_LINES})
    string(STRIP "${LINE}" _trimmed)

    if(_trimmed STREQUAL "${RV_CDEF_BEGIN_MARKER}")
      set(_in_region TRUE)
      continue()
    elseif(_trimmed STREQUAL "${RV_CDEF_END_MARKER}")
      set(_in_region FALSE)
      continue()
    endif()

    if(_in_region)
      # constants live OUTSIDE the marker regions, by construction
      continue()
    endif()

    if(NOT _trimmed MATCHES "^#define[ \t]+RV_([A-Za-z0-9_]+)[ \t]+(.+)$")
      continue()
    endif()
    set(_name "${CMAKE_MATCH_1}")
    set(_rawvalue "${CMAKE_MATCH_2}")

    # belt-and-suspenders: skip include guards by name shape too (their bare
    # `#define NAME` with no value already fails the match above)
    if(_name MATCHES "^PDK_.*_H$")
      continue()
    endif()

    # drop a trailing "// ..." comment from the value
    string(REGEX REPLACE "[ \t]*//.*$" "" _value "${_rawvalue}")
    string(STRIP "${_value}" _value)

    set(_literal "")

    if(_value MATCHES "^-?[0-9]+$")
      # a plain decimal integer
      set(_literal "${_value}")
    elseif(_value MATCHES "^0[xX][0-9a-fA-F]+$")
      # a plain hex integer
      set(_literal "${_value}")
    elseif(_value MATCHES "^U?INT(8|16|32|64)_C\\(([^()]+)\\)$")
      # a fixed-width literal macro wrapping a plain integer
      set(_inner "${CMAKE_MATCH_2}")
      string(STRIP "${_inner}" _inner)
      if(_inner MATCHES "^-?[0-9]+$" OR _inner MATCHES "^0[xX][0-9a-fA-F]+$")
        set(_literal "${_inner}")
      endif()
    elseif(_value MATCHES "^\\((.+)\\)$")
      # a simple integer expression: a parenthesized "|" of RV_ constants
      # that were themselves already resolved above (define-before-use, same
      # as C) - e.g. RV_HAPTIC_TARGET_BOTH. Lua has no `|` operator, so this
      # is evaluated here into a plain literal, not passed through as text.
      set(_inner "${CMAKE_MATCH_1}")
      string(REPLACE "|" ";" _terms "${_inner}")
      set(_ok TRUE)
      set(_acc "0")
      foreach(_term ${_terms})
        string(STRIP "${_term}" _term)
        if(NOT _term MATCHES "^RV_[A-Za-z0-9_]+$")
          set(_ok FALSE)
          break()
        endif()
        list(FIND RV_CONST_KEYS "${_term}" _idx)
        if(_idx EQUAL -1)
          set(_ok FALSE)
          break()
        endif()
        list(GET RV_CONST_VALUES ${_idx} _termval)
        math(EXPR _acc "${_acc} | (${_termval})" OUTPUT_FORMAT DECIMAL)
      endforeach()
      if(_ok)
        set(_literal "${_acc}")
      endif()
    endif()

    if(_literal STREQUAL "")
      # not a plain integer or a simple integer expression - skip it
      continue()
    endif()

    list(APPEND RV_CONST_KEYS "RV_${_name}")
    list(APPEND RV_CONST_VALUES "${_literal}")
    rv_escape_cxx_string_line("    ${_name} = ${_literal}," _escaped_entry)
    string(APPEND RV_CONSTS_BODY_TEXT "    \"${_escaped_entry}\\n\"\n")
  endforeach()
endforeach()

string(REGEX REPLACE "\n$" "" RV_CONSTS_BODY_TEXT "${RV_CONSTS_BODY_TEXT}")

# ---------------------------------------------------------------------------
# Emit the .cpp
# ---------------------------------------------------------------------------

set(RV_CDEF_CPP_CONTENT "// Generated by cmake/rv_cdef_gen.cmake - DO NOT EDIT.
//
// LuaJIT's ffi.cdef() takes a string of C declarations; it cannot follow
// #include or expand #define. These two strings carry the PDK's contract
// into the executable for the console's Lua side to consume:
//
//   rv_pdk_cdef   - fed to ffi.cdef(): the PDK's declarable types/functions
//   rv_pdk_consts - Lua source (\"return { ... }\"): the PDK's #define
//                   constants, which ffi.cdef cannot carry

extern const char rv_pdk_cdef[] =
${RV_CDEF_BODY_TEXT}
    ;

extern const char rv_pdk_consts[] =
    \"return {\\n\"
${RV_CONSTS_BODY_TEXT}
    \"}\\n\"
    ;
")

file(WRITE "${RV_CDEF_OUTPUT}" "${RV_CDEF_CPP_CONTENT}")
message(STATUS "rv_cdef_gen: wrote ${RV_CDEF_OUTPUT}")
