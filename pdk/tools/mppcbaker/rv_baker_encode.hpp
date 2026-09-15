// mppcbaker: encoding decoded pixels into .mppctex bytes (median-cut palette, IDX4/IDX8/direct15).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pdk/cv/rv_texel.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"

// One decoded pixel of the source, already reduced to console precision, plus
// the verdict on whether it is a hole.
struct src_pixel {
    rv_color5 color;
    bool transparent = false;
};
// What the user asked for. Both optionals are absent until the flag that fills
// them appears, so "was not given" cannot be mistaken for a value - there is no
// spare `int` state and no companion bool to keep in step.
struct options {
    std::string input;
    std::string output;
    std::optional<rv_texfmt> format;
    std::optional<rv_color> key; // the colour that becomes a hole
    bool help = false;
};
// What a failed step hands back to the CLI. `show_usage` is set by argument
// parsing only; an empty message means the failure has already explained itself
// on stderr, which is how getopt reports an unknown flag.
struct baker_error {
    std::string message;
    bool show_usage = false;
};
// The whole source, reduced once to console precision. Nothing downstream reads
// bytes from stb or asks how many channels the PNG had.
struct source_image {
    std::vector<src_pixel> pixels;
    int width = 0;
    int height = 0;
    size_t transparent_count = 0;
};

rv_err encode_texture(const options &opt, const source_image &src, std::vector<uint8_t> *out,
    baker_error *error);
