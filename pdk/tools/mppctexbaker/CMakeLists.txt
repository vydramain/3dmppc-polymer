# mppctexbaker BAKES a texture for the console: PNG in, .mppctex out. Sounds are
# baked by mppcmusbaker; a WAV given here is refused.

# --- stb_image (vendored, TOOL-ONLY dependency) -------------------------------
# The header lives in the repository, not on the network: see
# third_party/README.md for why. A tool that cannot be built on a plane is a
# poor tool, and pdk/tools ships as its own package.
#
# The console decodes no image format and never will, so this reaches exactly
# one target and cannot drift into src/ by accident.
set(MPPCTEXBAKER_STB_DIR ${RV_REPO_ROOT}/third_party/stb)
if(NOT EXISTS ${MPPCTEXBAKER_STB_DIR}/stb_image.h)
  message(FATAL_ERROR
    "mppctexbaker: third_party/stb/stb_image.h is missing. It is vendored, not "
    "downloaded - restore it from the repository rather than fetching it.")
endif()

add_executable(mppctexbaker mppctexbaker.cpp rv_baker_encode.cpp rv_baker_quantize.cpp)

# SYSTEM: stb_image.h is third-party and not warning-clean under -Wextra. Its
# implementation is textually part of mppctexbaker.cpp, but the diagnostics still
# point INTO the header, so a system include directory silences exactly stb's
# noise and leaves every warning about our own code standing.
target_include_directories(mppctexbaker SYSTEM PRIVATE ${MPPCTEXBAKER_STB_DIR})

# The contract, for pdk/cv/rv_texel.h - the transparent-texel value the
# console's sampler recognises. The baker writes that number into every file it
# produces, so it takes the number from the same header the console reads it
# from rather than restating it.
target_include_directories(mppctexbaker PRIVATE ${RV_REPO_ROOT}/pdk/include)

# pdklib, for pdklib/rv_stdio.hpp - the output adapter every tool prints through.
target_include_directories(mppctexbaker PRIVATE ${RV_REPO_ROOT}/pdk/lib/include)

if(MSVC)
  target_compile_options(mppctexbaker PRIVATE /W4)
else()
  target_compile_options(mppctexbaker PRIVATE -Wall -Wextra)
endif()
