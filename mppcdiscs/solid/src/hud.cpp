// 2D HUD + overlays (Agent E), drawn straight into the 320x240 framebuffer after
// the 3D pass. Everything here is pixel-level: a tiny built-in 5x7 bitmap font, a
// crosshair, HP bar + low-HP vignette, weapon/ammo/cooldown/charge readout, the
// interact prompt, the ritual progress bar, and the day-summary / game-over
// screens. Pure read of GameState — never mutated.
#include "hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game_state.hpp"

namespace rv_3dmppc {

namespace {

// ---- Low-level pixel ops (framebuffer::plot has no bounds check) ----------

void px(rv_Framebuffer& fb, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= fb.width() || y >= fb.height()) return;
    fb.plot(x, y, c);
}

Color readPx(const rv_Framebuffer& fb, int x, int y) {
    return fb.pixels()[static_cast<std::size_t>(y) * fb.width() + x];
}

Color mulColor(Color c, float f) {
    const auto r = static_cast<std::uint8_t>(((c >> 16) & 0xFF) * f);
    const auto g = static_cast<std::uint8_t>(((c >> 8) & 0xFF) * f);
    const auto b = static_cast<std::uint8_t>((c & 0xFF) * f);
    return rgb(r, g, b);
}

Color lerpColor(Color a, Color b, float t) {
    auto ch = [&](int sh) {
        const float av = static_cast<float>((a >> sh) & 0xFF);
        const float bv = static_cast<float>((b >> sh) & 0xFF);
        return static_cast<std::uint8_t>(av + (bv - av) * t);
    };
    return rgb(ch(16), ch(8), ch(0));
}

void fillRect(rv_Framebuffer& fb, int x, int y, int w, int h, Color c) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) px(fb, x + i, y + j, c);
}

void rectBorder(rv_Framebuffer& fb, int x, int y, int w, int h, Color c) {
    for (int i = 0; i < w; ++i) { px(fb, x + i, y, c); px(fb, x + i, y + h - 1, c); }
    for (int j = 0; j < h; ++j) { px(fb, x, y + j, c); px(fb, x + w - 1, y + j, c); }
}

// ---- Tiny 5x7 bitmap font -------------------------------------------------
// Each glyph is 7 rows; the low 5 bits of each row are the pixels (bit4 = left).
// Uppercase-only — text is upper-cased before lookup, unknowns render blank.

struct Glyph { char c; std::uint8_t rows[7]; };

const Glyph kFont[] = {
    {'0', {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}},
    {'1', {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}},
    {'2', {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}},
    {'3', {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110}},
    {'4', {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}},
    {'5', {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}},
    {'6', {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}},
    {'7', {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}},
    {'8', {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}},
    {'9', {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}},
    {'A', {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}},
    {'B', {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}},
    {'C', {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110}},
    {'D', {0b11100,0b10010,0b10001,0b10001,0b10001,0b10010,0b11100}},
    {'E', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}},
    {'F', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}},
    {'G', {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111}},
    {'H', {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}},
    {'I', {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}},
    {'J', {0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100}},
    {'K', {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}},
    {'L', {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}},
    {'M', {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}},
    {'N', {0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}},
    {'O', {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}},
    {'P', {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}},
    {'Q', {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}},
    {'R', {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}},
    {'S', {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}},
    {'T', {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}},
    {'U', {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}},
    {'V', {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}},
    {'W', {0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010}},
    {'X', {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}},
    {'Y', {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}},
    {'Z', {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}},
    {'.', {0b00000,0b00000,0b00000,0b00000,0b00000,0b00110,0b00110}},
    {',', {0b00000,0b00000,0b00000,0b00000,0b00110,0b00110,0b01000}},
    {':', {0b00000,0b00110,0b00110,0b00000,0b00110,0b00110,0b00000}},
    {'-', {0b00000,0b00000,0b00000,0b01110,0b00000,0b00000,0b00000}},
    {'/', {0b00001,0b00010,0b00010,0b00100,0b01000,0b01000,0b10000}},
    {'!', {0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100}},
    {'?', {0b01110,0b10001,0b00010,0b00100,0b00100,0b00000,0b00100}},
    {'[', {0b01110,0b01000,0b01000,0b01000,0b01000,0b01000,0b01110}},
    {']', {0b01110,0b00010,0b00010,0b00010,0b00010,0b00010,0b01110}},
    {'+', {0b00000,0b00100,0b00100,0b11111,0b00100,0b00100,0b00000}},
    {'%', {0b11001,0b11010,0b00100,0b01000,0b10011,0b00011,0b00000}},
};

const std::uint8_t* glyphRows(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.c == c) return g.rows;
    return nullptr;  // space + unknowns => blank cell
}

// Advance is 6*scale px per char (5 wide + 1 gap).
int textWidth(const char* s, int scale) {
    return static_cast<int>(std::strlen(s)) * 6 * scale;
}

void drawText(rv_Framebuffer& fb, int x, int y, const char* s, Color c, int scale = 1) {
    int cx = x;
    for (const char* p = s; *p; ++p, cx += 6 * scale) {
        const std::uint8_t* rows = glyphRows(*p);
        if (!rows) continue;
        for (int ry = 0; ry < 7; ++ry)
            for (int rx = 0; rx < 5; ++rx)
                if (rows[ry] & (1 << (4 - rx)))
                    fillRect(fb, cx + rx * scale, y + ry * scale, scale, scale, c);
    }
}

void drawTextCentered(rv_Framebuffer& fb, int cx, int y, const char* s, Color c, int scale = 1) {
    drawText(fb, cx - textWidth(s, scale) / 2, y, s, c, scale);
}

// ---- HUD widgets ----------------------------------------------------------

void drawCrosshair(rv_Framebuffer& fb) {
    const int cx = fb.width() / 2, cy = fb.height() / 2;
    const Color c = rgb(230, 230, 220);
    for (int i = 2; i <= 5; ++i) {
        px(fb, cx + i, cy, c); px(fb, cx - i, cy, c);
        px(fb, cx, cy + i, c); px(fb, cx, cy - i, c);
    }
    px(fb, cx, cy, c);
}

void drawHpBar(rv_Framebuffer& fb, const GameState& gs) {
    const float ratio = gs.player.maxHp > 0.0f
                            ? clampf(gs.player.hp / gs.player.maxHp, 0.0f, 1.0f)
                            : 0.0f;
    const int x = 8, y = fb.height() - 16, w = 90, h = 8;
    fillRect(fb, x, y, w, h, rgb(20, 18, 22));
    // Healthy → low colour shift (muted green-grey → alarm red).
    const Color hi = rgb(120, 150, 90), lo = rgb(180, 40, 40);
    const Color fillC = lerpColor(lo, hi, ratio);
    fillRect(fb, x + 1, y + 1, static_cast<int>((w - 2) * ratio), h - 2, fillC);
    rectBorder(fb, x, y, w, h, rgb(200, 200, 190));
    drawText(fb, x, y - 9, "HP", rgb(200, 200, 190));
}

// Darken the screen edges with a 4x4 ordered dither as HP drops (low-HP mood).
void drawLowHpVignette(rv_Framebuffer& fb, const GameState& gs) {
    const float ratio = gs.player.maxHp > 0.0f ? gs.player.hp / gs.player.maxHp : 1.0f;
    if (ratio >= 0.35f) return;
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6},
                                    {3, 11, 1, 9}, {15, 7, 13, 5}};
    const float sev = clampf((0.35f - ratio) / 0.35f, 0.0f, 1.0f);  // 0..1
    const int W = fb.width(), H = fb.height();
    const int margin = 70;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int edge = std::min(std::min(x, W - 1 - x), std::min(y, H - 1 - y));
            if (edge >= margin) continue;
            // 0 at the very edge → 1 at the margin; darken more near the edge.
            const float d = (1.0f - static_cast<float>(edge) / margin) * sev;
            const int threshold = static_cast<int>(d * 16.0f);
            if (bayer[y & 3][x & 3] < threshold)
                px(fb, x, y, mulColor(readPx(fb, x, y), 0.45f));
        }
}

void drawWeaponReadout(rv_Framebuffer& fb, const GameState& gs) {
    const WeaponState& w = gs.weapons;
    const bool brick = (w.active == WeaponState::Brick);
    const int rx = fb.width() - 8;
    const int ry = fb.height() - 16;
    const Color label = rgb(210, 205, 190);

    // Weapon name (right-aligned).
    const char* name = brick ? "BRICK" : "PIPE";
    drawText(fb, rx - textWidth(name, 1), ry, name, label);

    // Cooldown dims the readout; a small bar shows how much is left.
    const float cd = brick ? w.brickCooldown : w.pipeCooldown;
    if (cd > 0.0f) {
        const float c = clampf(cd, 0.0f, 1.0f);
        fillRect(fb, rx - 40, ry - 5, static_cast<int>(40 * c), 2, rgb(150, 120, 60));
    }

    if (brick) {
        // Ammo count "xN".
        char buf[8];
        std::snprintf(buf, sizeof(buf), "X%d", w.brickAmmo);
        drawText(fb, rx - textWidth(buf, 1), ry - 10, buf,
                 w.brickAmmo > 0 ? label : rgb(160, 60, 60));

        // Charge meter while readying a throw.
        if (w.brickCharge > 0.0f) {
            const int cx = fb.width() / 2 + 10, cy = fb.height() / 2;
            const int bw = 40, bh = 4;
            rectBorder(fb, cx, cy, bw, bh, rgb(180, 180, 170));
            fillRect(fb, cx + 1, cy + 1,
                     static_cast<int>((bw - 2) * clampf(w.brickCharge, 0.0f, 1.0f)),
                     bh - 2, rgb(210, 170, 70));
        }
    }
}

void drawPrompt(rv_Framebuffer& fb, const GameState& gs) {
    if (!gs.prompt) return;
    const int cx = fb.width() / 2;
    const int y = fb.height() - 40;
    // Backing plate for legibility over the 3D scene.
    const int w = textWidth(gs.prompt, 1) + 6;
    fillRect(fb, cx - w / 2, y - 2, w, 11, rgb(15, 14, 20));
    drawTextCentered(fb, cx, y, gs.prompt, rgb(225, 220, 200));
}

void drawRitualBar(rv_Framebuffer& fb, const GameState& gs) {
    const RitualState& r = gs.ritual;
    if (!(r.available || r.active)) return;
    const int cx = fb.width() / 2;
    const int y = 40;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "STEP %d/%d", r.step, RitualState::kSteps);
    drawTextCentered(fb, cx, y, buf, rgb(210, 200, 180));

    const int bw = 120, bh = 8, bx = cx - bw / 2, by = y + 10;
    fillRect(fb, bx, by, bw, bh, rgb(20, 18, 22));
    fillRect(fb, bx + 1, by + 1,
             static_cast<int>((bw - 2) * clampf(r.progress, 0.0f, 1.0f)), bh - 2,
             rgb(200, 170, 80));
    rectBorder(fb, bx, by, bw, bh, rgb(190, 180, 150));
}

void dimScreen(rv_Framebuffer& fb, float f) {
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            px(fb, x, y, mulColor(readPx(fb, x, y), f));
}

void drawSummary(rv_Framebuffer& fb, const GameState& gs) {
    dimScreen(fb, 0.3f);
    const int cx = fb.width() / 2;
    drawTextCentered(fb, cx, 70, "DAY COMPLETE", rgb(225, 215, 190), 2);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "DAY %d", gs.loop.day);
    drawTextCentered(fb, cx, 110, buf, rgb(210, 205, 190));
    std::snprintf(buf, sizeof(buf), "CORRUPTION: %d", gs.loop.corruption);
    drawTextCentered(fb, cx, 124, buf, rgb(200, 150, 120));
    drawTextCentered(fb, cx, 160, "PRESS [INTERACT]", rgb(180, 175, 160));
}

void drawGameOver(rv_Framebuffer& fb) {
    dimScreen(fb, 0.25f);
    const int cx = fb.width() / 2;
    drawTextCentered(fb, cx, 90, "KNOCKED OUT", rgb(200, 60, 60), 2);
    drawTextCentered(fb, cx, 130, "YOU WERE KNOCKED OUT", rgb(200, 190, 180));
    drawTextCentered(fb, cx, 150, "PRESS [START]", rgb(180, 175, 160));
}

const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Home:     return "HOME";
        case Phase::Street:   return "STREET";
        case Phase::Factory:  return "FACTORY";
        case Phase::Return:   return "RETURN";
        case Phase::GameOver: return "GAMEOVER";
    }
    return "?";
}

}  // namespace

// ---- Public entry points --------------------------------------------------

void drawHud(const GameState& gs, rv_Framebuffer& fb) {
    // Full-screen states take over the HUD.
    if (gs.loop.phase == Phase::GameOver) { drawGameOver(fb); return; }
    if (gs.loop.showSummary)              { drawSummary(fb, gs); return; }

    // In-world gameplay HUD.
    drawLowHpVignette(fb, gs);
    drawCrosshair(fb);
    drawHpBar(fb, gs);
    drawWeaponReadout(fb, gs);
    drawRitualBar(fb, gs);
    drawPrompt(fb, gs);
}

void drawDebugOverlay(const GameState& gs, rv_Framebuffer& fb, float frameMs) {
    char buf[32];
    const Color c = rgb(120, 230, 120);
    std::snprintf(buf, sizeof(buf), "MS %d.%d", static_cast<int>(frameMs),
                  static_cast<int>(frameMs * 10) % 10);
    drawText(fb, 4, 4, buf, c);
    std::snprintf(buf, sizeof(buf), "ENEMIES %d", gs.enemies.alive);
    drawText(fb, 4, 12, buf, c);
    std::snprintf(buf, sizeof(buf), "HP %d", static_cast<int>(gs.player.hp));
    drawText(fb, 4, 20, buf, c);
    drawText(fb, 4, 28, phaseName(gs.loop.phase), c);
}

}  // namespace rv_3dmppc
