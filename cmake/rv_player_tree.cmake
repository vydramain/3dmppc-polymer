# The player delivery tree: the player console with Steam Linux Runtime 4, written into
# RV_PCONSOLE_DIR (this build's delivery directory, see CMakeLists.txt) by the player-tree
# target, which is part of ALL. The steps are in cmake/rv_player_delivery.cmake.
if(NOT 3DMPPC_DEVTOOLS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT 3DMPPC_STATIC_DISC AND 3DMPPC_LUAJIT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_dv_version.cmake)

  set(RV_PLAYER_TREE ${RV_PCONSOLE_DIR})
  set(RV_PLAYER_ARGS
      "-DRV_PLAYER_ELF=$<TARGET_FILE:3dmppc>"
      "-DRV_PLAYER_LAUNCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_launch.sh"
      "-DRV_PLAYER_RUNTIME=${RV_STEAMRT4_DIR}"
      "-DRV_PLAYER_LUAJIT_COPYRIGHT=${luajit_SOURCE_DIR}/COPYRIGHT"
      "-DRV_PLAYER_LUAJIT_COMMIT=${RV_LUAJIT_COMMIT}"
      "-DRV_PLAYER_VER_MAJOR=${RV_MPPC_VER_MAJOR}"
      "-DRV_PLAYER_VER_MINOR=${RV_MPPC_VER_MINOR}"
      "-DRV_PLAYER_TREE=${RV_PLAYER_TREE}")

  add_custom_target(player-tree ALL
    DEPENDS 3dmppc
    COMMAND ${CMAKE_COMMAND} ${RV_PLAYER_ARGS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rv_player_delivery.cmake"
    COMMENT "Building the player delivery tree in ${RV_PCONSOLE_DIR}"
    VERBATIM
  )
endif()
if(NOT 3DMPPC_DEVTOOLS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN
   AND NOT 3DMPPC_STATIC_DISC AND NOT 3DMPPC_LUAJIT)
  message(STATUS "no player-tree: the player delivery needs 3DMPPC_LUAJIT=ON (its LICENSES/ and VERSIONS.txt name LuaJIT)")
endif()
