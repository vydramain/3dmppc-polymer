// mppcbaker: median-cut palette generation and Lloyd relaxation over rv_color5.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pdk/cv/rv_texel.h"

struct color_bin {
    rv_color5 color;
    uint32_t count = 0; // how many source pixels carry this exact colour
};

std::vector<rv_color5> median_cut(std::vector<color_bin> bins, size_t want);

size_t nearest(const std::vector<rv_color5> &palette, const rv_color5 &c);

void refine(const std::vector<color_bin> &bins, std::vector<rv_color5> &palette, int passes);
