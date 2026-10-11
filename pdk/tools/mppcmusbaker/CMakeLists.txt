# mppcmusbaker BAKES a sound: WAV in, headerless S16LE mono PCM out. The WAV reader
# is rv_musbaker_wav.cpp in this directory.

add_executable(mppcmusbaker mppcmusbaker.cpp rv_musbaker_wav.cpp)

# The contract headers: the version comes from pdk/, the output adapter from pdklib/.
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/include)
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)

if(MSVC)
  target_compile_options(mppcmusbaker PRIVATE /W4)
else()
  target_compile_options(mppcmusbaker PRIVATE -Wall -Wextra)
endif()
