# mppcinspector INSPECTS a prepared disc directory and prints its findings; --report PATH also writes a report file.
# Its disc check (rv_inspector_disc.hpp) runs the kit's scene checks (pdkkit/scene), which pdklib must not include.

# pdkkit is pulled in here, not at the top of pdk/tools, so the other tools keep their dependency boundary.
include(${RV_REPO_ROOT}/cmake/pdkkit.cmake)

# The disc check and its pdkkit link. A program that runs the check links this target.
add_library(mppcinspector_disc INTERFACE)
target_include_directories(mppcinspector_disc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mppcinspector_disc INTERFACE 3dmppc_pdkkit)

add_executable(mppcinspector
  main.cpp
  rv_inspector_run.cpp
)

target_link_libraries(mppcinspector PRIVATE mppcinspector_disc)

# pdklib/rv_logs/rv_logs.hpp is reached through pdkkit; without it every line prints "????".
target_compile_definitions(mppcinspector PRIVATE RV_LOG_ORIGIN="tool")

if(MSVC)
  target_compile_options(mppcinspector PRIVATE /W4)
else()
  target_compile_options(mppcinspector PRIVATE -Wall -Wextra)
endif()
