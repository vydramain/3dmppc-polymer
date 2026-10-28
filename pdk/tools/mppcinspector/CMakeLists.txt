# mppcinspector INSPECTS a prepared disc directory and prints its findings; --report PATH also writes a report file.
# It alone of the tools links pdkkit: it runs the kit's scene checks (pdkkit/scene), which pdklib must not include.

add_executable(mppcinspector
  main.cpp
  rv_inspector_run.cpp
)

# pdkkit is pulled in here, not at the top of pdk/tools, so the other tools keep their dependency boundary.
include(${RV_REPO_ROOT}/cmake/pdkkit.cmake)
target_link_libraries(mppcinspector PRIVATE 3dmppc_pdkkit)

# pdklib/rv_logs/rv_logs.hpp is reached through pdkkit; without it every line prints "????".
target_compile_definitions(mppcinspector PRIVATE RV_LOG_ORIGIN="tool")

if(MSVC)
  target_compile_options(mppcinspector PRIVATE /W4)
else()
  target_compile_options(mppcinspector PRIVATE -Wall -Wextra)
endif()
