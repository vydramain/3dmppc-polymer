// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 8 — HSV→RGB, смешивание и упаковка цвета для дисков.
// ──────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cmath>
#include <cstdint>

#include "pdk/cv/rv_vertex.hpp"
#include "sdk/rv_math.hpp"

namespace rv_3dmppc {

// Colour arithmetic for discs. rv_color (pdk/cv/rv_vertex.hpp) is three uint8
// channels and nothing else — the PDK is a hardware contract, not a maths
// library, so every disc that wants a hue sweep or a lit face has so far written
// the same conversions by hand. They live here now.
//
// The working type is float in [0, 1] per channel; rv_color is the packed form
// handed to the console. Conversions saturate rather than wrap: an over-bright
// channel must clip to white, never fold back to black.

// Pack one float channel. Rounds to nearest and saturates; NaN lands on 0.
inline uint8_t rv_color_channel(float value) {
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

inline rv_color rv_color_make(float r, float g, float b) {
    return rv_color{rv_color_channel(r), rv_color_channel(g), rv_color_channel(b)};
}

inline rv_vec3 rv_color_to_vec3(rv_color c) {
    return rv_vec3{static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                   static_cast<float>(c.b) / 255.0f};
}

inline rv_color rv_color_from_vec3(rv_vec3 v) { return rv_color_make(v.x, v.y, v.z); }

// THEOREM: HSV->RGB, six-sector piecewise-linear conversion. The hue circle is cut
// into six 60-degree sectors; inside a sector exactly ONE channel ramps linearly
// while the other two sit pinned at their maximum (v) and minimum (p = v(1-s)).
// That is precisely a walk along the surface path of the RGB cube, which is what
// makes the result continuous at every sector boundary — the ramping channel
// always arrives at the value the next sector starts it from. `f` is the position
// inside the sector, `q` the falling ramp and `t` the rising one; the switch only
// names which channel plays which role.
//
// `h` is in TURNS, not degrees: a disc animates a phase that grows forever, and
// turns make the wrap one floor(). s and v are [0, 1] and are used as given.
inline rv_color rv_hsv_to_rgb(float h, float s, float v) {
    h -= std::floor(h);  // wrap into [0, 1)

    const float sector = h * 6.0f;
    const int index = static_cast<int>(sector) % 6;
    const float f = sector - std::floor(sector);

    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));

    float r = v;
    float g = v;
    float b = v;
    switch (index) {
        case 0:
            r = v, g = t, b = p;
            break;
        case 1:
            r = q, g = v, b = p;
            break;
        case 2:
            r = p, g = v, b = t;
            break;
        case 3:
            r = p, g = q, b = v;
            break;
        case 4:
            r = t, g = p, b = v;
            break;
        default:
            r = v, g = p, b = q;
            break;
    }
    return rv_color_make(r, g, b);
}

// Linear blend, t = 0 -> a, t = 1 -> b. Clamped, so a caller feeding an
// unnormalised parameter gets the endpoint instead of a wrapped colour.
//
// Blending happens in the stored 8-bit channels, i.e. in whatever transfer curve
// the console displays them with. Perceptually "correct" blending would need a
// linearisation the hardware does not describe, and a PSX did not do it either.
inline rv_color rv_color_lerp(rv_color a, rv_color b, float t) {
    if (!(t > 0.0f)) return a;
    if (t >= 1.0f) return b;
    const rv_vec3 va = rv_color_to_vec3(a);
    const rv_vec3 vb = rv_color_to_vec3(b);
    return rv_color_from_vec3(va + (vb - va) * t);
}

// Multiply a colour by a scalar — the shading operation: same hue, less light.
inline rv_color rv_color_scale(rv_color c, float k) {
    return rv_color_from_vec3(rv_color_to_vec3(c) * k);
}

// Channel-wise product, the "modulate" of a texture against a vertex colour.
inline rv_color rv_color_modulate(rv_color a, rv_color b) {
    const rv_vec3 va = rv_color_to_vec3(a);
    const rv_vec3 vb = rv_color_to_vec3(b);
    return rv_color_from_vec3(rv_vec3{va.x * vb.x, va.y * vb.y, va.z * vb.z});
}

// Saturating sum — additive light on a surface.
inline rv_color rv_color_add(rv_color a, rv_color b) {
    return rv_color_from_vec3(rv_color_to_vec3(a) + rv_color_to_vec3(b));
}

// THEOREM: Lambert shading. A perfectly diffuse surface scatters light equally in
// every direction, so its brightness depends only on how obliquely the light
// arrives: proportional to cos(angle) between the surface normal and the
// direction TOWARDS the light, i.e. a dot product of two unit vectors. Negative
// values mean the light is behind the surface and clamp to zero; `ambient` is the
// floor that keeps unlit faces from going pure black, the era's substitute for
// bounced light.
//
// The console has no lighting stage at all — this bakes into the vertex COLOURS
// the disc submits, which the rasterizer then interpolates (Gouraud shading, for
// free, because differing vertex colours already mean a gradient).
inline rv_color rv_shade_lambert(rv_color base, rv_vec3 normal, rv_vec3 to_light, float ambient) {
    const float lambert = rv_dot(rv_normalize(normal), rv_normalize(to_light));
    float intensity = ambient + (lambert > 0.0f ? lambert * (1.0f - ambient) : 0.0f);
    if (intensity > 1.0f) intensity = 1.0f;
    return rv_color_scale(base, intensity);
}

}  // namespace rv_3dmppc
