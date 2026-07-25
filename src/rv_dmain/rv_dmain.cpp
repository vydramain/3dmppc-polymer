// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — логика диска: фазы цвета, движение спрайта, сборка кадра.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_dmain.hpp"

#include <cmath>

#include "pdk/cio/rv_cio.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/rv_err.hpp"

namespace rv_3dmppc {
namespace {

// --- baked assumptions, validated against the machine in disc_initialize ---

// Below this the layout below degenerates (the sprite alone is 24 px and the
// shapes are laid out in fractions of the screen).
constexpr int64_t RV_DMAIN_MIN_SCREEN = 64;

// The show files four primitives; the slack leaves room to grow without another
// contract query.
constexpr int64_t RV_DMAIN_MIN_FRAME_CAPACITY = 8;

// Port 0 steers the sprite, so at least one slot must exist. It may well be
// EMPTY — that is legal and simply means the sprite drifts on its own.
constexpr int64_t RV_DMAIN_MIN_PORTS = 1;

// --- show parameters ---

constexpr float RV_DMAIN_HUE_RATE = 0.15f;     // turns per second (~6.7 s per cycle)
constexpr float RV_DMAIN_STICK_SPEED = 90.0f;  // pixels per second at full deflection
constexpr float RV_DMAIN_SPRITE_SIZE = 24.0f;  // the driven square, in pixels

// Ordering-table probes: four distinct sort keys, drawn far-to-near. If the
// console gets the direction wrong the sprite ends up BEHIND the wireframe and
// the bug is visible in one glance.
constexpr int32_t RV_DMAIN_DEPTH_WIREFRAME = -100;
constexpr int32_t RV_DMAIN_DEPTH_TRIANGLE = 0;
constexpr int32_t RV_DMAIN_DEPTH_LINE = 200;
constexpr int32_t RV_DMAIN_DEPTH_SPRITE = 400;

// Colour channels are 8-bit; the show works in floats and lands here once.
uint8_t to_channel(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

// Screen coordinates are int16 (a vertex may sit off-screen and is clipped), so
// the float layout is rounded and saturated once, here.
int16_t to_screen(float value) {
    const float rounded = std::floor(value + 0.5f);
    if (rounded <= -32768.0f) return -32768;
    if (rounded >= 32767.0f) return 32767;
    return static_cast<int16_t>(rounded);
}

// THEOREM: HSV→RGB, six-sector piecewise-linear conversion. The PDK carries no
// colour utilities (it is a hardware contract, not a maths library), so the disc
// owns this. The hue circle is cut into six 60° sectors; inside a sector exactly
// one channel ramps linearly while the other two sit at their max (v) and min
// (p = v(1-s)) — that is what makes the RGB cube's surface path continuous. `f`
// is the position within the sector, `q` the falling ramp and `t` the rising
// one; the switch just names which channel plays which role in each sector.
rv_color hsv_to_rgb(float h, float s, float v) {
    h -= std::floor(h);  // wrap into [0, 1) — the caller's phase grows forever

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
    return rv_color{to_channel(r), to_channel(g), to_channel(b)};
}

// PATTERN: factory method. rv_primitive is a tagged UNION and its members are
// plain structs with no constructors, so an "almost filled" literal leaves live
// fields holding whatever the stack had. These builders fill EVERY field of the
// active member, once, so no call site can forget one.
rv_vertex make_vertex(float x, float y, rv_color color) {
    rv_vertex vertex{};
    vertex.x = to_screen(x);
    vertex.y = to_screen(y);
    vertex.color = color;
    vertex.uv = rv_uv{0, 0};  // read only when fill_mode samples a texture
    return vertex;
}

}  // namespace

int64_t rv_dmain::disc_initialize(rv_pdko& pdk) {
    pdk_ = &pdk;

    rv_cv* cv = pdk_->cv();
    rv_cio* cio = pdk_->cio();
    if (!cv || !cio) return RV_ERR_INVAL;

    screen_width_ = cv->screen_width();
    screen_height_ = cv->screen_height();
    frame_capacity_ = cv->frame_capacity();
    iport_count_ = cio->iport_count();

    // The point of this hook: the disc bakes assumptions, the machine answers,
    // and a mismatch is refused HERE rather than discovered as garbage on screen
    // half a second in. The console reports the negative code to the user.
    if (screen_width_ < RV_DMAIN_MIN_SCREEN || screen_height_ < RV_DMAIN_MIN_SCREEN) {
        return RV_ERR_INVAL;
    }
    if (frame_capacity_ < RV_DMAIN_MIN_FRAME_CAPACITY) return RV_ERR_INVAL;
    if (iport_count_ < RV_DMAIN_MIN_PORTS) return RV_ERR_INVAL;

    // One previous-mask slot per port, allocated once: frame_update must not
    // allocate, and the port count is session-stable.
    prev_buttons_.assign(static_cast<std::size_t>(iport_count_), 0ULL);

    sprite_x_ = static_cast<float>(screen_width_) * 0.5f;
    sprite_y_ = static_cast<float>(screen_height_) * 0.5f;

    return RV_OK;
}

void rv_dmain::frame_update(float dt) {
    if (!pdk_) return;
    if (dt < 0.0f) dt = 0.0f;  // a non-monotonic clock must not run the show backwards

    // Colour phase. It only ever grows, so one floor() keeps it in [0, 1).
    hue_ += dt * RV_DMAIN_HUE_RATE;
    hue_ -= std::floor(hue_);

    rv_cio* cio = pdk_->cio();

    // Port 0's left stick takes over the sprite when it leaves the dead zone
    // (the console has already applied and rescaled that zone — a resting stick
    // reads as an exact 0). Otherwise the square drifts by itself, so the show
    // proves the frame loop is alive even with no controller plugged in.
    const rv_istate pad = cio->iport_state(0);
    const bool steered = pad.left_stick.x != 0.0f || pad.left_stick.y != 0.0f;
    if (steered) {
        // The contract's +y is UP; rv_vertex/rv_sprite y grows DOWN. The sign
        // flip is the whole translation between the two conventions.
        sprite_x_ += pad.left_stick.x * RV_DMAIN_STICK_SPEED * dt;
        sprite_y_ -= pad.left_stick.y * RV_DMAIN_STICK_SPEED * dt;
    } else {
        sprite_x_ += drift_x_ * dt;
        sprite_y_ += drift_y_ * dt;
    }

    // Reflect off the edges: clamp the centre, then point the drift back inward.
    // Steering hits the same clamp, so the square can never be pushed off-screen.
    const float half = RV_DMAIN_SPRITE_SIZE * 0.5f;
    const float max_x = static_cast<float>(screen_width_) - half;
    const float max_y = static_cast<float>(screen_height_) - half;
    if (sprite_x_ < half) {
        sprite_x_ = half;
        drift_x_ = std::fabs(drift_x_);
    } else if (sprite_x_ > max_x) {
        sprite_x_ = max_x;
        drift_x_ = -std::fabs(drift_x_);
    }
    if (sprite_y_ < half) {
        sprite_y_ = half;
        drift_y_ = std::fabs(drift_y_);
    } else if (sprite_y_ > max_y) {
        sprite_y_ = max_y;
        drift_y_ = -std::fabs(drift_y_);
    }

    // THEOREM: edge detection by snapshot diff. rv_cio reports the CURRENT level
    // of every source and never a press/release event, so "was it pressed THIS
    // frame" is (now & ~was) — the bits that are set now and were clear a frame
    // ago. Acting on the level instead would fire the shutdown once per frame
    // for as long as the key is held; only the rising edge is an intent.
    for (int64_t port = 0; port < iport_count_; ++port) {
        const uint64_t now = cio->iport_state(port).buttons;
        uint64_t& was = prev_buttons_[static_cast<std::size_t>(port)];

        // MENU is Esc on the keyboard, Option/Start on a pad — any port may ask.
        if ((now & ~was) & RV_ISOURCE_MENU_BTTN_MENU) release_ = true;

        was = now;
    }
}

void rv_dmain::frame_render() {
    if (!pdk_) return;

    // No simulation lives here: a headless run never calls frame_render(), and
    // the show must still advance identically (rv_de::frame_render).
    rv_cv* cv = pdk_->cv();

    const float width = static_cast<float>(screen_width_);
    const float height = static_cast<float>(screen_height_);

    // The background walks the hue circle at a lowered saturation/value so the
    // primitives on top stay readable against it.
    cv->frame_configure(0, hsv_to_rgb(hue_, 0.7f, 0.9f));

    // Farthest: a wireframe quad framing the screen. Proves the WIREFRAME fill
    // mode draws edges only — filled, it would swallow everything behind it.
    {
        const float inset = 8.0f;
        const rv_color edge = hsv_to_rgb(hue_ + 0.5f, 0.9f, 1.0f);

        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_POLYGON;
        primitive.depth = RV_DMAIN_DEPTH_WIREFRAME;

        rv_polygon& quad = primitive.data.polygon;
        quad.fill_mode = RV_PRIMITIVE_FILL_MODE_WIREFRAME;
        quad.addr_texture = 0;  // unread: this fill mode never samples
        quad.addr_palette = 0;
        quad.mapping = RV_TEXWRAP_CLAMP;
        quad.vertex_count = 4;
        // A quad splits into triangles (1,2,3) and (2,3,4), so the order is
        // Z-shaped rather than a loop around the rectangle (rv_primitives.hpp).
        quad.vertexes[0] = make_vertex(inset, inset, edge);
        quad.vertexes[1] = make_vertex(width - inset, inset, edge);
        quad.vertexes[2] = make_vertex(inset, height - inset, edge);
        quad.vertexes[3] = make_vertex(width - inset, height - inset, edge);

        cv->frame_put(primitive);
    }

    // A gouraud triangle: three different vertex colours, no texture. If the
    // rasterizer interpolates per-vertex colour this is a smooth blend; if it
    // does not, it is three flat wedges or one flat fill.
    {
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_POLYGON;
        primitive.depth = RV_DMAIN_DEPTH_TRIANGLE;

        rv_polygon& triangle = primitive.data.polygon;
        triangle.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        triangle.addr_texture = 0;
        triangle.addr_palette = 0;
        triangle.mapping = RV_TEXWRAP_CLAMP;
        triangle.vertex_count = 3;
        triangle.vertexes[0] =
            make_vertex(width * 0.5f, height * 0.18f, hsv_to_rgb(hue_ + 0.10f, 1.0f, 1.0f));
        triangle.vertexes[1] =
            make_vertex(width * 0.20f, height * 0.72f, hsv_to_rgb(hue_ + 0.43f, 1.0f, 1.0f));
        triangle.vertexes[2] =
            make_vertex(width * 0.80f, height * 0.72f, hsv_to_rgb(hue_ + 0.76f, 1.0f, 1.0f));
        // vertexes[3] is ignored at vertex_count == 3, but the union member is
        // filled anyway so no byte of the submitted primitive is indeterminate.
        triangle.vertexes[3] = make_vertex(0.0f, 0.0f, rv_color{0, 0, 0});

        cv->frame_put(primitive);
    }

    // A diagonal line with different end colours — the 1D case of the same
    // interpolation, and the cheapest way to see it go wrong.
    {
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_LINE;
        primitive.depth = RV_DMAIN_DEPTH_LINE;

        rv_line& line = primitive.data.line;
        line.vertexes[0] =
            make_vertex(width * 0.08f, height * 0.88f, hsv_to_rgb(hue_ + 0.25f, 1.0f, 1.0f));
        line.vertexes[1] =
            make_vertex(width * 0.92f, height * 0.12f, hsv_to_rgb(hue_ + 0.75f, 1.0f, 1.0f));

        cv->frame_put(primitive);
    }

    // Nearest: the driven square, on top of everything above.
    {
        const float half = RV_DMAIN_SPRITE_SIZE * 0.5f;

        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_SPRITE;
        primitive.depth = RV_DMAIN_DEPTH_SPRITE;

        rv_sprite& sprite = primitive.data.sprite;
        sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        sprite.addr_texture = 0;
        sprite.addr_palette = 0;
        sprite.color = hsv_to_rgb(hue_ + 0.5f, 0.35f, 1.0f);
        sprite.mapping = RV_TEXWRAP_CLAMP;
        sprite.x = to_screen(sprite_x_ - half);  // the struct takes the UPPER-LEFT corner
        sprite.y = to_screen(sprite_y_ - half);
        sprite.width = static_cast<uint16_t>(RV_DMAIN_SPRITE_SIZE);
        sprite.height = static_cast<uint16_t>(RV_DMAIN_SPRITE_SIZE);

        cv->frame_put(primitive);
    }

    // The console does NOT flush for the disc (rv_de::frame_render). Nothing
    // filed above reaches the screen without this call.
    cv->frame_flush();
}

const char* rv_dmain::disc_title() const { return "rv_dmain - light show"; }

}  // namespace rv_3dmppc
