# mppcrepairer REPAIRS a prepared disc directory with the kit's repair rules, then checks it again.
# It gets pdkkit through mppcinspector_disc, the disc check mppcinspector runs; it does not include pdkkit.cmake itself.

add_executable(mppcrepairer
  main.cpp
  rv_repairer_run.cpp
)

target_link_libraries(mppcrepairer PRIVATE mppcinspector_disc)

# rv_logs.hpp stamps every line with this origin; pdkkit's headers reach it, so without the define lines print "????".
target_compile_definitions(mppcrepairer PRIVATE RV_LOG_ORIGIN="tool")

if(MSVC)
  target_compile_options(mppcrepairer PRIVATE /W4)
else()
  target_compile_options(mppcrepairer PRIVATE -Wall -Wextra)
endif()
