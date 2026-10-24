include_guard(DIRECTORY)

# The product version is read from the contract header, so the number lives in one place.
# Sets RV_MPPC_VER_MAJOR and RV_MPPC_VER_MINOR; include()d from the top-level CMakeLists.txt and cmake/rv_webkit.cmake.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pdk/include/pdk/de/rv_dv.h RV_MPPC_DV_HEADER)
# A version change in the header reconfigures the build; the player zip name and the web kit's kit.txt depend on it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pdk/include/pdk/de/rv_dv.h)
string(REGEX MATCH "#define RV_MPPC_VER_MAJOR ([0-9]+)" _ "${RV_MPPC_DV_HEADER}")
set(RV_MPPC_VER_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define RV_MPPC_VER_MINOR ([0-9]+)" _ "${RV_MPPC_DV_HEADER}")
set(RV_MPPC_VER_MINOR "${CMAKE_MATCH_1}")
# "0" is false in CMake, so a minor version of 0 is checked against the empty string.
if(RV_MPPC_VER_MAJOR STREQUAL "" OR RV_MPPC_VER_MINOR STREQUAL "")
  message(FATAL_ERROR "pdk/include/pdk/de/rv_dv.h has no '#define RV_MPPC_VER_MAJOR <n>' or RV_MPPC_VER_MINOR <n>")
endif()
