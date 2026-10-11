# mppcmusbaker BAKES a sound: WAV in, headerless S16LE mono PCM out. The WAV reader
# is mppcbaker's own rv_baker_wav.cpp, compiled from its directory, so the two tools
# cannot disagree about what a sound file holds.

set(MPPCMUSBAKER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mppcbaker)

add_executable(mppcmusbaker mppcmusbaker.cpp ${MPPCMUSBAKER_SRC_DIR}/rv_baker_wav.cpp)

# rv_baker_encode.hpp, for baker_error, which the WAV reader's interface names.
target_include_directories(mppcmusbaker PRIVATE ${MPPCMUSBAKER_SRC_DIR})

# The same contract headers as mppcbaker: the version comes from pdk/, the output
# adapter from pdklib/.
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/include)
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)

if(MSVC)
  target_compile_options(mppcmusbaker PRIVATE /W4)
else()
  target_compile_options(mppcmusbaker PRIVATE -Wall -Wextra)
endif()
