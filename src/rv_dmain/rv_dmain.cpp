// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 4-8 — сервисный тест: текстуры, привод, карта, звук, ввод.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_dmain.hpp"

#include <cmath>

#include "pdk/ca/rv_ca.hpp"
#include "pdk/cd/rv_cd.hpp"
#include "pdk/cio/rv_cio.hpp"
#include "pdk/cm/rv_cm.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/rv_err.hpp"
#include "sdk/rv_camera.hpp"
#include "sdk/rv_color.hpp"
#include "sdk/rv_math.hpp"
#include "sdk/rv_xform.hpp"

namespace rv_3dmppc {
namespace {

// --- baked assumptions, validated against the machine in disc_initialize ---

constexpr int64_t RV_DMAIN_MIN_SCREEN = 64;
constexpr int64_t RV_DMAIN_MIN_FRAME_CAPACITY = 16;
constexpr int64_t RV_DMAIN_MIN_PORTS = 1;

// --- the test texture ---

// 16x16 DIRECT15. Small enough to build by hand, big enough that TILE and
// STRETCH look different from CLAMP at a glance.
constexpr int64_t RV_DMAIN_TEX_SIZE = 16;

// Remember the contract's rule: 0000h is a HOLE, not black (opaque black is
// 8000h). The fourth quadrant is deliberately holes so the cut-out path is
// visible, and the border is white so edge behaviour is unmistakable.
constexpr uint16_t RV_DMAIN_TEXEL_RED = 0x001F;
constexpr uint16_t RV_DMAIN_TEXEL_GREEN = 0x03E0;
constexpr uint16_t RV_DMAIN_TEXEL_BLUE = 0x7C00;
constexpr uint16_t RV_DMAIN_TEXEL_WHITE = 0x7FFF;
constexpr uint16_t RV_DMAIN_TEXEL_HOLE = 0x0000;

// --- the beep ---

constexpr int64_t RV_DMAIN_BEEP_RATE = 44100;    // the console's fixed sample rate
constexpr int64_t RV_DMAIN_BEEP_FRAMES = 22050;  // half a second
constexpr float RV_DMAIN_BEEP_HZ = 440.0f;
constexpr int16_t RV_DMAIN_BEEP_PEAK = 20000;

// --- the show ---

constexpr float RV_DMAIN_HUE_RATE = 0.05f;
constexpr float RV_DMAIN_SPIN_RATE = 0.7f;
constexpr uint32_t RV_DMAIN_SAVE_MAGIC = 0x524D4149;  // 'RMAI'

// Ordering-table probes. Larger depth = nearer, so the wrap row sits behind
// the cube and the status bars sit in front of it.
constexpr int32_t RV_DMAIN_DEPTH_WRAP_ROW = -900;
constexpr int32_t RV_DMAIN_DEPTH_BARS = 800;

// The unit cube: six quads, each in the PDK's Z ORDER, because the console
// splits a quad into triangles (1,2,3) and (2,3,4) rather than walking the rim.
// Wound counter-clockwise as seen from OUTSIDE, which is what
// RV_CULL_SCREEN_CW keeps.
struct rv_dmain_face {
    rv_vec3 corner[4];
    rv_vec3 normal;
};

constexpr rv_dmain_face RV_DMAIN_CUBE[6] = {
    {{{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}, {0, 0, -1}},
    {{{1, -1, 1}, {-1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}, {0, 0, 1}},
    {{{1, -1, -1}, {1, -1, 1}, {1, 1, -1}, {1, 1, 1}}, {1, 0, 0}},
    {{{-1, -1, 1}, {-1, -1, -1}, {-1, 1, 1}, {-1, 1, -1}}, {-1, 0, 0}},
    {{{-1, 1, -1}, {1, 1, -1}, {-1, 1, 1}, {1, 1, 1}}, {0, 1, 0}},
    {{{-1, -1, 1}, {1, -1, 1}, {-1, -1, -1}, {1, -1, -1}}, {0, -1, 0}},
};

int16_t to_screen(float value) {
    const float rounded = std::floor(value + 0.5f);
    if (rounded <= -32768.0f) return -32768;
    if (rounded >= 32767.0f) return 32767;
    return static_cast<int16_t>(rounded);
}

// A flat rectangle, filled in completely so no byte of the union is left
// indeterminate. PATTERN: factory method — rv_primitive is a tagged union of
// constructor-less structs, and an "almost filled" literal leaves live fields
// holding whatever the stack had.
rv_primitive make_bar(float x, float y, float w, float h, rv_color color, int32_t depth) {
    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_SPRITE;
    primitive.depth = depth;

    rv_sprite& sprite = primitive.data.sprite;
    sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    sprite.addr_texture = 0;
    sprite.addr_palette = 0;
    sprite.color = color;
    sprite.mapping = RV_TEXWRAP_CLAMP;
    sprite.x = to_screen(x);
    sprite.y = to_screen(y);
    sprite.width = static_cast<uint16_t>(w < 0.0f ? 0.0f : w);
    sprite.height = static_cast<uint16_t>(h < 0.0f ? 0.0f : h);
    return primitive;
}

}  // namespace

// --- initialization ----------------------------------------------------------

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
    // and a mismatch is refused HERE rather than discovered as garbage on
    // screen half a second in. The console reports the negative code and stops.
    if (screen_width_ < RV_DMAIN_MIN_SCREEN || screen_height_ < RV_DMAIN_MIN_SCREEN) {
        return RV_ERR_INVAL;
    }
    if (frame_capacity_ < RV_DMAIN_MIN_FRAME_CAPACITY) return RV_ERR_INVAL;
    if (iport_count_ < RV_DMAIN_MIN_PORTS) return RV_ERR_INVAL;

    // One previous-mask slot per port, allocated once: frame_update must not
    // allocate, and the port count is session-stable.
    prev_buttons_.assign(static_cast<std::size_t>(iport_count_), 0ULL);

    build_texture();
    probe_drive();
    load_save();
    build_beep();

    return RV_OK;
}

void rv_dmain::build_texture() {
    rv_cv* cv = pdk_->cv();

    if (RV_DMAIN_TEX_SIZE > cv->texture_max_width() ||
        RV_DMAIN_TEX_SIZE > cv->texture_max_height()) {
        return;  // the machine is smaller than this disc assumed; skip, do not lie
    }

    texels_.assign(static_cast<std::size_t>(RV_DMAIN_TEX_SIZE * RV_DMAIN_TEX_SIZE),
                   RV_DMAIN_TEXEL_HOLE);

    const int64_t half = RV_DMAIN_TEX_SIZE / 2;
    for (int64_t y = 0; y < RV_DMAIN_TEX_SIZE; ++y) {
        for (int64_t x = 0; x < RV_DMAIN_TEX_SIZE; ++x) {
            uint16_t texel;
            if (x < half && y < half) {
                texel = RV_DMAIN_TEXEL_RED;
            } else if (x >= half && y < half) {
                texel = RV_DMAIN_TEXEL_GREEN;
            } else if (x < half && y >= half) {
                texel = RV_DMAIN_TEXEL_BLUE;
            } else {
                texel = RV_DMAIN_TEXEL_HOLE;  // the cut-out quadrant
            }

            const bool border =
                x == 0 || y == 0 || x == RV_DMAIN_TEX_SIZE - 1 || y == RV_DMAIN_TEX_SIZE - 1;
            if (border) texel = RV_DMAIN_TEXEL_WHITE;

            texels_[static_cast<std::size_t>(y * RV_DMAIN_TEX_SIZE + x)] = texel;
        }
    }

    const int64_t bytes = static_cast<int64_t>(texels_.size() * sizeof(uint16_t));
    const int64_t addr = cv->video_asset_malloc(bytes);
    if (addr < 0) return;

    rv_texture texture{};
    texture.format = RV_TEXFMT_DIRECT15;
    texture.data = texels_.data();
    texture.size = static_cast<uint64_t>(bytes);
    texture.width = static_cast<uint64_t>(RV_DMAIN_TEX_SIZE);
    texture.height = static_cast<uint64_t>(RV_DMAIN_TEX_SIZE);

    if (cv->video_asset_write(addr, texture) < 0) {
        cv->video_asset_free(addr);
        return;
    }
    addr_texture_ = addr;
}

void rv_dmain::probe_drive() {
    rv_cd* cd = pdk_->cd();
    if (!cd) return;

    // A name carrying path separators must be refused before anything touches
    // the medium — it is an attempt to leave the disc, not a spelling mistake.
    // This probe asserts the drive answers INVAL rather than merely NOENT.
    drive_rejects_paths_ = cd->asset_open("../../etc/passwd") == RV_ERR_INVAL &&
                           cd->asset_open("assets/thing.obj") == RV_ERR_INVAL;

    const int64_t handle = cd->asset_open("protagonist.obj");
    if (handle < 0) return;  // no medium inserted is a legal, quiet outcome

    const int64_t size = cd->asset_size(handle);
    if (size <= 0) return;

    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    // AUTHORITATIVE is what asset_read returns, not what asset_size promised:
    // the contract calls the size a hint that may go stale between the calls.
    const int64_t read = cd->asset_read(handle, buffer.data(), size);
    if (read < 0) return;

    asset_bytes_ = read;
    asset_ok_ = read > 0 && buffer[0] == '#';  // a Wavefront .obj opens with a comment

    // The same name must resolve to the same handle, forever.
    if (cd->asset_open("protagonist.obj") != handle) asset_ok_ = false;
}

void rv_dmain::load_save() {
    rv_cm* cm = pdk_->cm();
    if (!cm) return;

    if (cm->card_slot_size() < static_cast<int64_t>(sizeof(rv_dmain_save))) {
        return;  // this machine's slots cannot hold our blob
    }

    rv_dmain_save blob{RV_DMAIN_SAVE_MAGIC, 0};

    const int64_t size = cm->card_size(0);
    if (size >= 0) {
        rv_dmain_save stored{};
        if (cm->card_read(0, &stored, static_cast<int64_t>(sizeof(stored))) >= 0 &&
            stored.magic == RV_DMAIN_SAVE_MAGIC) {
            blob.boot_count = stored.boot_count;
        }
    } else if (size != RV_ERR_NOENT) {
        // A real medium failure. NOT the same as "no save yet": overwriting now
        // would destroy a save that may well be there, so play without one.
        return;
    }

    ++blob.boot_count;
    if (cm->card_write(0, &blob, static_cast<int64_t>(sizeof(blob))) == RV_OK) {
        boot_count_ = blob.boot_count;
        card_ok_ = true;
    }
}

void rv_dmain::build_beep() {
    rv_ca* ca = pdk_->ca();
    if (!ca || ca->voice_count() < 1) return;

    // THEOREM: an exponentially decaying sine. The console has no pitch control,
    // so a sample plays at exactly the rate it was recorded for — the frequency
    // is baked in here, against RV_DMAIN_BEEP_RATE, and nothing downstream can
    // bend it. The decay lives in the SAMPLE rather than the envelope so the
    // beep stays recognisable even with an ADSR nobody has tuned.
    std::vector<int16_t> pcm(static_cast<std::size_t>(RV_DMAIN_BEEP_FRAMES));
    for (int64_t i = 0; i < RV_DMAIN_BEEP_FRAMES; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(RV_DMAIN_BEEP_RATE);
        const float envelope = std::exp(-6.0f * t);
        const float wave = std::sin(6.28318531f * RV_DMAIN_BEEP_HZ * t);
        pcm[static_cast<std::size_t>(i)] =
            static_cast<int16_t>(wave * envelope * static_cast<float>(RV_DMAIN_BEEP_PEAK));
    }

    const int64_t bytes = static_cast<int64_t>(pcm.size() * sizeof(int16_t));
    const int64_t addr = ca->sound_asset_malloc(bytes);
    if (addr < 0) return;

    rv_sample sample{};
    sample.data = pcm.data();
    sample.size = bytes;
    if (ca->sound_asset_write(addr, &sample) < 0) {
        ca->sound_asset_free(addr);
        return;
    }

    rv_voice_conf conf{};
    conf.voice = 1;  // voice 0
    conf.loop_type = rv_loop::none;
    conf.sample_address = addr;
    conf.ar = 5;
    conf.dr = 40;
    conf.sr = 0;
    conf.rr = 120;
    conf.sl = 26000;
    conf.volume = 32767;
    conf.volume_l = 32767;
    conf.volume_r = 32767;
    if (ca->voice_setup(&conf) < 0) return;

    addr_beep_ = addr;
    beep_ok_ = true;
}

// --- simulation --------------------------------------------------------------

void rv_dmain::frame_update(float dt) {
    if (!pdk_) return;
    if (dt < 0.0f) dt = 0.0f;  // a non-monotonic clock must not run the show backwards

    hue_ += dt * RV_DMAIN_HUE_RATE;
    hue_ -= std::floor(hue_);
    spin_ += dt * RV_DMAIN_SPIN_RATE;

    rv_cio* cio = pdk_->cio();
    rv_ca* ca = pdk_->ca();

    // THEOREM: edge detection by snapshot diff. rv_cio reports the CURRENT level
    // of every source and never a press/release event, so "was it pressed THIS
    // frame" is (now & ~was) — set now, clear a frame ago. Acting on the level
    // would fire the beep sixty times a second for as long as the key is held;
    // only the rising edge is an intent.
    for (int64_t port = 0; port < iport_count_; ++port) {
        const uint64_t now = cio->iport_state(port).buttons;
        uint64_t& was = prev_buttons_[static_cast<std::size_t>(port)];
        const uint64_t pressed = now & ~was;
        was = now;

        // Esc on the keyboard, Option/Start on a pad — any port may ask.
        if (pressed & RV_ISOURCE_MENU_BTTN_MENU) release_ = true;

        if ((pressed & RV_ISOURCE_FRONT_BTTN_SOUTH) && beep_ok_ && ca) {
            ca->voice_play(1);
        }
    }
}

// --- the frame ---------------------------------------------------------------

void rv_dmain::frame_render() {
    if (!pdk_) return;

    // No simulation lives here: a headless run never calls frame_render(), and
    // the show must advance identically either way (rv_de::frame_render).
    rv_cv* cv = pdk_->cv();

    // The Z flag is on because the cut-out row depends on it: a hole must write
    // neither colour nor depth, and that is only observable when per-pixel depth
    // resolves primitives sharing an ordering-table bucket.
    cv->frame_configure(RV_PIPELINE_BUFFER_CONFIG_TYPE_Z, rv_hsv_to_rgb(hue_, 0.5f, 0.35f));

    draw_wrap_row();
    draw_cube();
    draw_status_bars();

    // The console does NOT flush for the disc (rv_de::frame_render). Nothing
    // filed above reaches the screen without this call.
    cv->frame_flush();
}

void rv_dmain::draw_wrap_row() {
    if (addr_texture_ == 0) return;

    rv_cv* cv = pdk_->cv();

    // Three copies of one texture, one per wrap mode. CLAMP shows it once and
    // smears its edge over the rest; TILE repeats it; STRETCH scales the whole
    // thing to fit. In all three the fourth quadrant must show the background
    // through it — that is the 0000h cut-out rule.
    const rv_texture_mapping_type modes[3] = {RV_TEXWRAP_CLAMP, RV_TEXWRAP_TILE,
                                              RV_TEXWRAP_STRETCH};
    const float size = 48.0f;
    const float gap = 8.0f;
    const float total = 3.0f * size + 2.0f * gap;
    const float x0 = (static_cast<float>(screen_width_) - total) * 0.5f;
    const float y0 = 8.0f;

    for (int i = 0; i < 3; ++i) {
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_SPRITE;
        primitive.depth = RV_DMAIN_DEPTH_WRAP_ROW;

        rv_sprite& sprite = primitive.data.sprite;
        sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
        sprite.addr_texture = addr_texture_;
        sprite.addr_palette = 0;  // DIRECT15 carries its colour in the texel
        sprite.color = rv_color{255, 255, 255};
        sprite.mapping = modes[i];
        sprite.x = to_screen(x0 + static_cast<float>(i) * (size + gap));
        sprite.y = to_screen(y0);
        sprite.width = static_cast<uint16_t>(size);
        sprite.height = static_cast<uint16_t>(size);

        cv->frame_put(primitive);
    }
}

void rv_dmain::draw_cube() {
    rv_cv* cv = pdk_->cv();

    const float width = static_cast<float>(screen_width_);
    const float height = static_cast<float>(screen_height_);

    const rv_camera camera = rv_camera_make(rv_vec3{0.0f, 1.2f, -4.2f},  // eye
                                            rv_vec3{0.0f, 0.0f, 0.0f},   // target
                                            rv_vec3{0.0f, 1.0f, 0.0f},   // up
                                            1.0472f,                     // 60° vertical fov
                                            width / height, 0.1f, 100.0f);

    const rv_mat4 model = rv_mat4_mul(rv_mat4_rotate_y(spin_), rv_mat4_rotate_x(spin_ * 0.6f));

    // The depth window stays inside the wrap row and the bars, so the cube is
    // always between them and the ordering table is doing visible work.
    const rv_xform_conf conf = rv_xform_conf_make(rv_camera_mvp(camera, model), camera, width,
                                                  height, -800, 700, RV_CULL_SCREEN_CW);

    const rv_vec3 to_light{-0.4f, 0.8f, -0.5f};

    for (const rv_dmain_face& face : RV_DMAIN_CUBE) {
        // Lighting is in WORLD space, so the normal takes the model matrix. The
        // positions do NOT: rv_camera_mvp already carries the model matrix, and
        // transforming them here as well would spin the cube twice.
        const rv_color shade =
            rv_shade_lambert(rv_hsv_to_rgb(hue_ + 0.5f, 0.45f, 1.0f),
                             rv_mat4_mul_direction(model, face.normal), to_light, 0.25f);

        rv_xform_vertex vertexes[4];
        for (int i = 0; i < 4; ++i) {
            vertexes[i].position = face.corner[i];
            vertexes[i].color = shade;
            vertexes[i].uv = rv_uv{0, 0};
        }

        rv_primitive primitive{};
        if (!rv_xform_quad(conf, vertexes, primitive)) continue;  // culled or clipped away
        primitive.data.polygon.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        cv->frame_put(primitive);
    }
}

void rv_dmain::draw_status_bars() {
    rv_cv* cv = pdk_->cv();

    const float width = static_cast<float>(screen_width_);
    const float height = static_cast<float>(screen_height_);

    // Bottom-left: one tick per boot, so a restart is visible without a log.
    // Green means the card round-tripped, red means it refused.
    const rv_color card_color = card_ok_ ? rv_color{80, 220, 100} : rv_color{220, 70, 70};
    const uint32_t ticks = boot_count_ > 16 ? 16 : boot_count_;
    for (uint32_t i = 0; i < ticks; ++i) {
        cv->frame_put(make_bar(6.0f + static_cast<float>(i) * 6.0f, height - 12.0f, 4.0f, 6.0f,
                               card_color, RV_DMAIN_DEPTH_BARS));
    }

    // Bottom-right: the drive. A bar as wide as the asset is big (log-scaled, so
    // a 28 KB model does not run off the screen), cyan when the bytes came back
    // looking like themselves. Absent entirely when no medium is inserted —
    // which is a legal state, not a failure.
    if (asset_bytes_ > 0) {
        const float scaled = std::log2(static_cast<float>(asset_bytes_)) * 6.0f;
        const rv_color drive_color = asset_ok_ ? rv_color{80, 200, 220} : rv_color{220, 160, 60};
        cv->frame_put(make_bar(width - 6.0f - scaled, height - 22.0f, scaled, 6.0f, drive_color,
                               RV_DMAIN_DEPTH_BARS));
    }

    // Top-left: one square per passing probe — path rejection, then audio. Dark
    // means that probe did not pass, and the eye finds it instantly.
    cv->frame_put(make_bar(6.0f, 6.0f, 6.0f, 6.0f,
                           drive_rejects_paths_ ? rv_color{80, 220, 100} : rv_color{60, 60, 60},
                           RV_DMAIN_DEPTH_BARS));
    cv->frame_put(make_bar(16.0f, 6.0f, 6.0f, 6.0f,
                           beep_ok_ ? rv_color{80, 220, 100} : rv_color{60, 60, 60},
                           RV_DMAIN_DEPTH_BARS));
}

const char* rv_dmain::disc_title() const { return "rv_dmain - service test"; }

}  // namespace rv_3dmppc
