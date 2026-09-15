// mppcbaker: Heckbert median cut plus Lloyd relaxation, isolated from the file format.
#include "rv_baker_quantize.hpp"

#include <algorithm>
#include <cstdint>

#include "pdk/rv_err.h"

namespace {

// --- median cut ---------------------------------------------------------------

// A channel of rv_color5 is five bits, so 0..31 (pdk/cv/rv_texel.h).
constexpr int RV_BAKER_CHANNEL5_MAX = 31;

// Luma coefficients of ITU-R BT.601 (Y' = 0.299 R' + 0.587 G' + 0.114 B') taken
// x10 and rounded: 2.99 -> 3, 5.87 -> 6, 1.14 -> 1. They weight the colour
// metric below so that palette accuracy is spent on green, which the eye
// resolves far better than blue. Integers keep every comparison exact and
// therefore reproducible across compilers.
constexpr int RV_BAKER_LUMA_WEIGHT_R = 3;
constexpr int RV_BAKER_LUMA_WEIGHT_G = 6;
constexpr int RV_BAKER_LUMA_WEIGHT_B = 1;

// A box is a half-open SLICE of the bin array, not a geometric volume. It
// behaves like an axis-aligned box only because the slice is sorted along the
// splitting channel before every cut, which keeps both halves contiguous.
struct box {
    size_t begin = 0; // half-open range over the bin array
    size_t end = 0;
};

// The three channels as a closed set. An int would let any number through and
// channel_of would answer with blue for all of them, silently.
enum color_axis {
    COLOR_AXIS_R,
    COLOR_AXIS_G,
    COLOR_AXIS_B,
};

constexpr color_axis RV_BAKER_COLOR_AXES[] = { COLOR_AXIS_R, COLOR_AXIS_G, COLOR_AXIS_B };

uint8_t channel_of(const rv_color5 &c, color_axis axis)
{
    switch (axis) {
    case COLOR_AXIS_R:
        return c.r;
    case COLOR_AXIS_G:
        return c.g;
    case COLOR_AXIS_B:
        break;
    }
    return c.b; // the enum has no fourth value; this is the B arm
}

// What widest_axis answers. A pair would leave every caller remembering which of
// .first and .second is the channel and which is the length.
struct axis_span {
    color_axis axis = COLOR_AXIS_R;
    int span = 0;
};

// Longest edge of a box and the channel it runs along. The span is the crude
// measure of how badly one colour can stand in for the whole box: colours
// differing by 2 are already described by their mean, colours spanning 25 are
// not. Measured UNWEIGHTED - RV_BAKER_LUMA_WEIGHT_* applies to distance2, not here.
axis_span widest_axis(const std::vector<color_bin> &bins, const box &b)
{
    axis_span best{ COLOR_AXIS_R, -1 };
    for (const color_axis axis : RV_BAKER_COLOR_AXES) {
        // Seeded at the ends of the channel range, so the first colour takes both.
        int lo = RV_BAKER_CHANNEL5_MAX;
        int hi = 0;
        for (size_t i = b.begin; i < b.end; ++i) {
            const int v = channel_of(bins[i].color, axis);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const int span = hi - lo;
        if (span > best.span) {
            best.span = span;
            best.axis = axis;
        }
    }
    return best;
}

// The one colour that stands in for a whole box: the mean of its colours weighted
// by pixel count, so a colour covering half the box pulls the entry half the way.
//
// RV_ERR_INVAL means the box holds no pixels, which is an INVARIANT VIOLATION
// rather than an expected outcome: a bin is only created for a colour with a
// non-zero count, and a split never produces an empty half (see the clamp in
// median_cut). The guard exists so a broken invariant cannot divide by zero.
rv_err gen_color5(const box &b, const std::vector<color_bin> &bins, rv_color5 *out)
{
    uint64_t weight = 0;
    uint64_t sr = 0;
    uint64_t sg = 0;
    uint64_t sb = 0;
    for (size_t i = b.begin; i < b.end; ++i) {
        const uint64_t w = bins[i].count;
        weight += w;
        sr += static_cast<uint64_t>(bins[i].color.r) * w;
        sg += static_cast<uint64_t>(bins[i].color.g) * w;
        sb += static_cast<uint64_t>(bins[i].color.b) * w;
    }

    if (weight == 0) {
        return RV_ERR_INVAL;
    }

    // + weight/2 rounds to nearest; integer division alone always truncates down.
    *out = rv_color5{ static_cast<uint8_t>((sr + weight / 2) / weight),
        static_cast<uint8_t>((sg + weight / 2) / weight),
        static_cast<uint8_t>((sb + weight / 2) / weight) };

    return RV_OK;
}

// Chooses the box to split next: longest edge wins, ties broken by pixel
// population so a large flat area is refined before a stray gradient. A box
// holding one colour cannot be split and is skipped.
//
// A SEARCH, not a fallible operation: false means every box is already a single
// colour, which is the normal answer for an image with fewer colours than the
// palette has slots - nothing failed. `target` is untouched in that case.
bool pick_box(const std::vector<box> &bs, const std::vector<color_bin> &bins, size_t *target)
{
    size_t best = bs.size();
    int best_span = 0;
    uint64_t best_pop = 0;
    for (size_t i = 0; i < bs.size(); ++i) {
        if (bs[i].end - bs[i].begin < 2) {
            continue; // a single colour cannot be split further
        }
        const int span = widest_axis(bins, bs[i]).span;
        uint64_t pop = 0;
        for (size_t k = bs[i].begin; k < bs[i].end; ++k) {
            pop += bins[k].count;
        }
        if (span > best_span || (span == best_span && pop > best_pop)) {
            best = i;
            best_span = span;
            best_pop = pop;
        }
    }
    if (best == bs.size() || best_span == 0) {
        return false;
    }

    *target = best;
    return true;
}

// SQUARED distance between two colours under the RV_BAKER_LUMA_WEIGHT_* metric. Squared
// because every caller only compares results and sqrt is monotone: the ordering
// survives, while the arithmetic stays integer and therefore identical on every
// compiler - two near-equal candidates can never swap places on someone else's
// machine and bake a different file.
//
// The weights make this deliberately non-Euclidean: it is the squared length of
// the difference vector measured with the axes rescaled by sqrt(3), sqrt(6), 1.
// An error in blue is thus tolerated sqrt(6) ~ 2.45 times further than the same
// error in green. A finer metric that varies the weights with the red level is
// Riemersma's (https://www.compuphase.com/cmetric.htm); it is not used here.
uint32_t distance2(const rv_color5 &a, const rv_color5 &b)
{
    const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    return static_cast<uint32_t>(
        RV_BAKER_LUMA_WEIGHT_R * dr * dr + RV_BAKER_LUMA_WEIGHT_G * dg * dg + RV_BAKER_LUMA_WEIGHT_B * db * db);
}

} // namespace

// Median cut - Heckbert, "Color Image Quantization for Frame Buffer Display",
// SIGGRAPH '82 (doi:10.1145/965145.801294). The palette is built by repeatedly
// halving the colour set instead of searching for optimal centroids.
//
// One box starts out holding every distinct opaque colour. Until there are as
// many boxes as palette slots, the worst box is sorted along its widest channel
// and split at the median BY PIXEL COUNT; each surviving box then contributes
// one entry, the weighted mean of its colours.
//
// Chosen over plain k-means because it is deterministic - the same PNG always
// bakes byte-identical output, which is what makes a build cache and a diff of
// committed assets mean anything - and because it cannot waste a slot on an
// empty cluster the way a random seed can. Splitting by population rather than
// by colour count is what suits flat-shaded artwork: a colour covering half the
// image gets half the palette even from a visually tiny corner of the cube.
//
// Its weakness is that boxes are axis-aligned, so a colour ramp lying diagonally
// in the cube is cut clumsily; refine() below repairs that. pngquant's
// libimagequant pairs the two algorithms the same way.
std::vector<rv_color5> median_cut(std::vector<color_bin> bins, size_t want)
{
    std::vector<rv_color5> palette;
    if (bins.empty() || want == 0) {
        return palette;
    }

    std::vector<box> boxes{ box{ 0, bins.size() } };
    while (boxes.size() < want) {
        size_t target = 0;
        if (!pick_box(boxes, bins, &target)) {
            break; // the image has fewer distinct colours than there are slots
        }

        box &b = boxes[target];
        const color_axis axis = widest_axis(bins, b).axis;
        std::sort(bins.begin() + static_cast<ptrdiff_t>(b.begin),
            bins.begin() + static_cast<ptrdiff_t>(b.end),
            [axis](const color_bin &x, const color_bin &y) {
                return channel_of(x.color, axis) < channel_of(y.color, axis);
            });

        // Split where the cumulative PIXEL COUNT crosses half, not the colour count.
        uint64_t total = 0;
        for (size_t i = b.begin; i < b.end; ++i) {
            total += bins[i].count;
        }
        uint64_t acc = 0;
        size_t split = b.begin + 1;
        for (size_t i = b.begin; i + 1 < b.end; ++i) {
            acc += bins[i].count;
            if (acc * 2 >= total) {
                split = i + 1;
                break;
            }
            split = i + 2;
        }
        split = std::clamp(split, b.begin + 1, b.end - 1);

        const box right{ split, b.end };
        b.end = split;
        boxes.push_back(right);
    }

    for (const box &b : boxes) {
        rv_color5 color5 = {};
        if (gen_color5(b, bins, &color5) != RV_OK) {
            continue;
        }
        palette.push_back(color5);
    }
    return palette;
}

// Index of the palette entry closest to c. A linear scan is enough: the palette
// is at most 256 entries and this runs per DISTINCT colour, not per pixel. It
// only reads the palette - the entries themselves are moved by refine().
size_t nearest(const std::vector<rv_color5> &palette, const rv_color5 &c)
{
    size_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for (size_t i = 0; i < palette.size(); ++i) {
        const uint32_t d = distance2(palette[i], c);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

// Lloyd's algorithm - S. P. Lloyd, "Least Squares Quantization in PCM", Bell
// Labs 1957, published in IEEE Trans. Inf. Theory 28(2):129-137, 1982
// (doi:10.1109/TIT.1982.1056489); also known as Voronoi iteration, and as
// k-means when the initial centres are random. Here it relaxes the palette that
// median cut produced, repairing the ramps its axis-aligned boxes cut badly.
//
// One pass has two halves that must not be interleaved: assign every colour to
// its nearest entry, THEN move each entry to the weighted mean of what it
// received. The assignment reads the palette frozen at the start of the pass, so
// the result cannot depend on the order of `bins`.
//
// A pass cannot increase total error, and the weighted mean really is the
// minimiser under this metric: the weights are a constant factor in the
// derivative and cancel, which is why the mean needs no knowledge of
// RV_BAKER_LUMA_WEIGHT_*. Passes are counted, not run to convergence - see RV_BAKER_LLOYD_PASSES.
void refine(const std::vector<color_bin> &bins, std::vector<rv_color5> &palette, int passes)
{
    if (palette.empty()) {
        return;
    }
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<uint64_t> weight(palette.size(), 0);
        std::vector<uint64_t> sr(palette.size(), 0);
        std::vector<uint64_t> sg(palette.size(), 0);
        std::vector<uint64_t> sb(palette.size(), 0);
        for (const color_bin &bin : bins) {
            const size_t k = nearest(palette, bin.color);
            weight[k] += bin.count;
            sr[k] += static_cast<uint64_t>(bin.color.r) * bin.count;
            sg[k] += static_cast<uint64_t>(bin.color.g) * bin.count;
            sb[k] += static_cast<uint64_t>(bin.color.b) * bin.count;
        }
        for (size_t i = 0; i < palette.size(); ++i) {
            if (weight[i] == 0) {
                continue; // an orphaned entry is left where it is, not moved
            }
            // + weight/2 rounds to nearest; plain integer division truncates down.
            palette[i] = rv_color5{ static_cast<uint8_t>((sr[i] + weight[i] / 2) / weight[i]),
                static_cast<uint8_t>((sg[i] + weight[i] / 2) / weight[i]),
                static_cast<uint8_t>((sb[i] + weight[i] / 2) / weight[i]) };
        }
    }
}
