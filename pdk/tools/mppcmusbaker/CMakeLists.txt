# mppcmusbaker BAKES a sound: WAV or MP3 in, headerless S16LE mono PCM out. The WAV
# reader is rv_musbaker_wav.cpp, the MP3 decoder rv_musbaker_mp3.cpp.

add_executable(mppcmusbaker mppcmusbaker.cpp rv_musbaker_wav.cpp rv_musbaker_mp3.cpp)

# minimp3 is vendored (third_party/README.md), a single header with its code in
# rv_musbaker_mp3.cpp only. SYSTEM: it is third-party and not warning-clean under
# -Wextra, so the warnings it would raise are silenced while ours stay.
target_include_directories(mppcmusbaker SYSTEM PRIVATE ${RV_REPO_ROOT}/third_party/minimp3)

# The contract headers: the version comes from pdk/, the output adapter from pdklib/.
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/include)
target_include_directories(mppcmusbaker PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)

if(MSVC)
  target_compile_options(mppcmusbaker PRIVATE /W4)
else()
  target_compile_options(mppcmusbaker PRIVATE -Wall -Wextra)
endif()
