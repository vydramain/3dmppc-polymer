// Procedural asset library (Agent E). Builds a low-poly, PSX-legible mesh +
// (optional) tiny paletted texture for every MeshId. Everything is generated
// from a strongly-limited palette grounded in a 1990s Russian industrial tone
// (pale skin, dark navy, matte brown, black; grey street; rusty factory), so the
// whole disc is self-generating — the one exception is the protagonist, which we
// try to load from the disc's real model and cube-fall-back on failure.
//
// Conventions:
//   * Left-handed, +y up. Meshes are two-sided (the renderer leaves back-face
//     culling off), so triangle winding is not load-bearing here.
//   * Architecture is modelled at natural world scale; actors are pivoted at the
//     feet (y=0 = ground); hand-held / projectile items are centred on the
//     origin because gameplay tumbles and offsets them (see weapons.cpp).
//   * No pixel lighting exists in the rasterizer, so each box face is given a
//     fixed shade factor for a cheap "lit" read, and every mesh carries sensible
//     per-vertex colours so untextured meshes still show their material.
#include "assets_gen.hpp"

#include <cmath>
#include <optional>

#include "assets/image.hpp"
#include "assets/obj_loader.hpp"
#include "core/color.hpp"

namespace rv_3dmppc {

namespace {

// ---- Palette (limited, matte, industrial) --------------------------------
namespace pal {
const Vec3 skin{0.82f, 0.71f, 0.62f};
const Vec3 skinPale{0.86f, 0.80f, 0.74f};
const Vec3 navy{0.13f, 0.16f, 0.30f};
const Vec3 brown{0.31f, 0.22f, 0.14f};
const Vec3 wood{0.37f, 0.26f, 0.16f};
const Vec3 black{0.07f, 0.07f, 0.09f};
const Vec3 greyStreet{0.33f, 0.33f, 0.36f};
const Vec3 concrete{0.42f, 0.42f, 0.44f};
const Vec3 asphalt{0.20f, 0.20f, 0.23f};
const Vec3 rust{0.43f, 0.25f, 0.15f};
const Vec3 rustDark{0.30f, 0.18f, 0.11f};
const Vec3 metal{0.47f, 0.49f, 0.53f};
const Vec3 metalDark{0.30f, 0.32f, 0.35f};
const Vec3 brick{0.56f, 0.29f, 0.23f};
const Vec3 glass{0.24f, 0.34f, 0.42f};
const Vec3 dimYellow{0.66f, 0.60f, 0.26f};
const Vec3 lampGlow{0.85f, 0.78f, 0.42f};
const Vec3 smoke{0.62f, 0.63f, 0.66f};
const Vec3 blood{0.42f, 0.10f, 0.10f};
const Vec3 pickupGold{0.80f, 0.68f, 0.34f};
}  // namespace pal

// ---- Geometry helpers ----------------------------------------------------

// One flat quad (two triangles) a→b→c→d, flat-shaded with `col`.
void quad(Mesh& m, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
          const Vec3& col, float uMax = 1.0f, float vMax = 1.0f) {
    const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
    const Vec3 n = normalize(cross(b - a, c - a));
    m.vertices.push_back({a, n, {0.0f, 0.0f}, col});
    m.vertices.push_back({b, n, {uMax, 0.0f}, col});
    m.vertices.push_back({c, n, {uMax, vMax}, col});
    m.vertices.push_back({d, n, {0.0f, vMax}, col});
    m.indices.insert(m.indices.end(),
                     {base, base + 1, base + 2, base, base + 2, base + 3});
}

// Axis-aligned box between mn and mx, each face given a fixed shade for depth.
void box(Mesh& m, const Vec3& mn, const Vec3& mx, const Vec3& c) {
    const float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
    const Vec3 top = c * 1.0f, bot = c * 0.45f, fb = c * 0.82f, lr = c * 0.64f;
    quad(m, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, top);  // +y
    quad(m, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}, bot);  // -y
    quad(m, {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, fb);   // -z
    quad(m, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}, fb);   // +z
    quad(m, {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, lr);   // -x
    quad(m, {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, lr);   // +x
}

// Box from a centre + half-extents (handy for props/actors).
void boxC(Mesh& m, const Vec3& center, const Vec3& half, const Vec3& c) {
    box(m, center - half, center + half, c);
}

// Vertical prism/cylinder about (cx,cz), radius r, from y0 to y1, `sides` faces.
void cyl(Mesh& m, float cx, float cz, float r, float y0, float y1, int sides,
         const Vec3& c) {
    const Vec3 side = c * 0.8f, top = c * 1.0f, bot = c * 0.5f;
    for (int i = 0; i < sides; ++i) {
        const float a0 = static_cast<float>(i) / sides * 2.0f * kPi;
        const float a1 = static_cast<float>(i + 1) / sides * 2.0f * kPi;
        const Vec3 p0{cx + r * std::cos(a0), y0, cz + r * std::sin(a0)};
        const Vec3 p1{cx + r * std::cos(a1), y0, cz + r * std::sin(a1)};
        const Vec3 p2{cx + r * std::cos(a1), y1, cz + r * std::sin(a1)};
        const Vec3 p3{cx + r * std::cos(a0), y1, cz + r * std::sin(a0)};
        quad(m, p0, p1, p2, p3, side);
    }
    // Top + bottom caps as triangle fans.
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap ? y1 : y0;
        const Vec3 col = cap ? top : bot;
        const std::uint32_t centerIdx = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({{cx, y, cz}, {0, cap ? 1.0f : -1.0f, 0}, {0.5f, 0.5f}, col});
        const std::uint32_t ringStart = static_cast<std::uint32_t>(m.vertices.size());
        for (int i = 0; i < sides; ++i) {
            const float a = static_cast<float>(i) / sides * 2.0f * kPi;
            m.vertices.push_back({{cx + r * std::cos(a), y, cz + r * std::sin(a)},
                                  {0, cap ? 1.0f : -1.0f, 0},
                                  {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)},
                                  col});
        }
        for (int i = 0; i < sides; ++i) {
            const std::uint32_t a = ringStart + i;
            const std::uint32_t b = ringStart + (i + 1) % sides;
            m.indices.insert(m.indices.end(), {centerIdx, a, b});
        }
    }
}

// A small diamond (octahedron) — used for the pickup marker.
void octa(Mesh& m, const Vec3& center, float r, float h, const Vec3& c) {
    const Vec3 top{center.x, center.y + h, center.z};
    const Vec3 bot{center.x, center.y - h, center.z};
    const Vec3 e[4] = {{center.x + r, center.y, center.z},
                       {center.x, center.y, center.z + r},
                       {center.x - r, center.y, center.z},
                       {center.x, center.y, center.z - r}};
    for (int i = 0; i < 4; ++i) {
        const Vec3& p = e[i];
        const Vec3& q = e[(i + 1) % 4];
        // Upper and lower faces, brighter up top for a gem-like read.
        const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
        const Vec3 n1 = normalize(cross(q - top, p - top));
        m.vertices.push_back({top, n1, {0.5f, 1.0f}, c * 1.0f});
        m.vertices.push_back({p, n1, {0.0f, 0.0f}, c * 0.8f});
        m.vertices.push_back({q, n1, {1.0f, 0.0f}, c * 0.8f});
        const Vec3 n2 = normalize(cross(p - bot, q - bot));
        m.vertices.push_back({bot, n2, {0.5f, 0.0f}, c * 0.5f});
        m.vertices.push_back({p, n2, {0.0f, 1.0f}, c * 0.7f});
        m.vertices.push_back({q, n2, {1.0f, 1.0f}, c * 0.7f});
        m.indices.insert(m.indices.end(),
                         {base, base + 1, base + 2, base + 3, base + 4, base + 5});
    }
}

// A blocky humanoid pivoted at the feet: legs, torso, head, arms, eyes.
// `eye` bright-ish; passing eye == body hides them (no glow).
void figure(Mesh& m, float h, float halfW, const Vec3& body, const Vec3& head,
            const Vec3& eye) {
    const float legTop = 0.46f * h;
    const float torsoTop = 0.82f * h;
    const float legHW = halfW * 0.42f;
    // Legs
    boxC(m, {-legHW, legTop * 0.5f, 0.0f}, {legHW * 0.8f, legTop * 0.5f, halfW * 0.5f},
         body * 0.75f);
    boxC(m, {legHW, legTop * 0.5f, 0.0f}, {legHW * 0.8f, legTop * 0.5f, halfW * 0.5f},
         body * 0.75f);
    // Torso
    box(m, {-halfW, legTop, -halfW * 0.55f}, {halfW, torsoTop, halfW * 0.55f}, body);
    // Arms
    const float armHW = halfW * 0.28f;
    boxC(m, {-halfW - armHW, (legTop + torsoTop) * 0.5f, 0.0f},
         {armHW, (torsoTop - legTop) * 0.5f, halfW * 0.4f}, body * 0.9f);
    boxC(m, {halfW + armHW, (legTop + torsoTop) * 0.5f, 0.0f},
         {armHW, (torsoTop - legTop) * 0.5f, halfW * 0.4f}, body * 0.9f);
    // Head
    const float headHW = halfW * 0.62f;
    const Vec3 headC{0.0f, torsoTop + headHW, 0.0f};
    boxC(m, headC, {headHW, headHW, headHW}, head);
    // Eyes (tiny, on the -z face)
    if (!(eye.x == body.x && eye.y == body.y && eye.z == body.z)) {
        const float ez = headC.z - headHW - 0.01f;
        const float ey = headC.y + headHW * 0.15f;
        const Vec3 eSize{headHW * 0.22f, 0.03f, 0.02f};
        boxC(m, {-headHW * 0.4f, ey, ez}, eSize, eye);
        boxC(m, {headHW * 0.4f, ey, ez}, eSize, eye);
    }
}

// ---- Tiny paletted textures (multiply the vertex material) ---------------

// Grid/mortar/window patterns are grayscale MULTIPLIERS: white texels keep the
// material colour, darker texels stamp lines/windows into it. Kept 8x8, nearest.
rv_Texture brickTex() {
    const int s = 8;
    std::vector<Color> t(static_cast<std::size_t>(s) * s);
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const bool row = (y % 4 == 0);
            const bool head = ((x + (y / 4) * 2) % 4 == 0);
            t[static_cast<std::size_t>(y) * s + x] = (row || head) ? 0xFF6E5A50u : 0xFFFFFFFFu;
        }
    return rv_Texture(s, s, std::move(t));
}

rv_Texture facadeTex() {
    const int s = 8;
    std::vector<Color> t(static_cast<std::size_t>(s) * s);
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const bool win = ((x % 4 == 1) || (x % 4 == 2)) && ((y % 4 == 1) || (y % 4 == 2));
            t[static_cast<std::size_t>(y) * s + x] = win ? 0xFF303840u : 0xFFFFFFFFu;
        }
    return rv_Texture(s, s, std::move(t));
}

rv_Texture panelTex() {
    const int s = 8;
    std::vector<Color> t(static_cast<std::size_t>(s) * s);
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const bool line = (x % 4 == 0) || (y % 4 == 0);
            t[static_cast<std::size_t>(y) * s + x] = line ? 0xFF909090u : 0xFFFFFFFFu;
        }
    return rv_Texture(s, s, std::move(t));
}

}  // namespace

// ---- Registry build ------------------------------------------------------

void MeshRegistry::build() {
    auto M = [&](MeshId id) -> Mesh& { return meshes_[static_cast<int>(id)]; };
    auto T = [&](MeshId id) -> rv_Texture& { return textures_[static_cast<int>(id)]; };

    // ---------------- World: Home ----------------
    // Floor: thin wooden plate (scaled/tiled by the area system).
    box(M(MeshId::HomeFloor), {-2.0f, -0.05f, -2.0f}, {2.0f, 0.0f, 2.0f}, pal::wood);
    T(MeshId::HomeFloor) = rv_Texture::checker(8, 0xFFFFFFFFu, 0xFFB4A488u, 4);

    // Wall segment (3 m tall).
    box(M(MeshId::HomeWall), {-2.0f, 0.0f, -0.08f}, {2.0f, 3.0f, 0.08f},
        pal::navy * 1.4f);
    T(MeshId::HomeWall) = panelTex();

    // Bed: brown frame + navy blanket + pale pillow.
    box(M(MeshId::HomeBed), {-0.5f, 0.0f, -1.0f}, {0.5f, 0.28f, 1.0f}, pal::wood);
    box(M(MeshId::HomeBed), {-0.48f, 0.28f, -0.95f}, {0.48f, 0.42f, 0.9f}, pal::navy);
    box(M(MeshId::HomeBed), {-0.4f, 0.42f, 0.55f}, {0.4f, 0.55f, 0.95f}, pal::skinPale);

    // Radio: small brown box with a lighter dial panel.
    box(M(MeshId::HomeRadio), {-0.18f, 0.0f, -0.12f}, {0.18f, 0.22f, 0.12f}, pal::brown);
    box(M(MeshId::HomeRadio), {-0.14f, 0.05f, -0.13f}, {0.06f, 0.17f, -0.11f}, pal::dimYellow);
    box(M(MeshId::HomeRadio), {0.02f, 0.16f, -0.13f}, {0.14f, 0.2f, -0.115f}, pal::black);

    // Table: top + four legs.
    {
        Mesh& t = M(MeshId::HomeTable);
        box(t, {-0.6f, 0.7f, -0.4f}, {0.6f, 0.78f, 0.4f}, pal::wood);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                box(t, {sx * 0.54f - 0.04f, 0.0f, sz * 0.34f - 0.04f},
                    {sx * 0.54f + 0.04f, 0.7f, sz * 0.34f + 0.04f}, pal::wood * 0.85f);
    }

    // Window: frame ring + tinted glass pane.
    {
        Mesh& w = M(MeshId::HomeWindow);
        box(w, {-0.6f, 0.0f, -0.03f}, {0.6f, 0.1f, 0.03f}, pal::brown);   // bottom
        box(w, {-0.6f, 1.1f, -0.03f}, {0.6f, 1.2f, 0.03f}, pal::brown);   // top
        box(w, {-0.6f, 0.0f, -0.03f}, {-0.5f, 1.2f, 0.03f}, pal::brown);  // left
        box(w, {0.5f, 0.0f, -0.03f}, {0.6f, 1.2f, 0.03f}, pal::brown);    // right
        box(w, {-0.02f, 0.0f, -0.02f}, {0.02f, 1.2f, 0.02f}, pal::brown); // mullion
        box(w, {-0.5f, 0.1f, -0.01f}, {0.5f, 1.1f, 0.01f}, pal::glass);   // glass
    }

    // Generic mutation prop: a crate + a leaning poster plane.
    {
        Mesh& p = M(MeshId::HomeProp);
        box(p, {-0.2f, 0.0f, -0.2f}, {0.2f, 0.4f, 0.2f}, pal::brown);
        box(p, {-0.25f, 0.4f, -0.05f}, {0.25f, 0.9f, 0.0f}, pal::rust * 1.1f);
    }

    // ---------------- World: Street ----------------
    box(M(MeshId::StreetRoad), {-3.0f, -0.05f, -3.0f}, {3.0f, 0.0f, 3.0f}, pal::asphalt * 1.6f);
    T(MeshId::StreetRoad) = rv_Texture::checker(8, 0xFFFFFFFFu, 0xFFA0A0A8u, 4);

    // Facade: tall building face with a window grid.
    box(M(MeshId::StreetFacade), {-2.5f, 0.0f, -0.15f}, {2.5f, 6.0f, 0.15f}, pal::concrete);
    T(MeshId::StreetFacade) = facadeTex();

    // Fence: vertical bars + top rail.
    {
        Mesh& f = M(MeshId::StreetFence);
        box(f, {-1.5f, 1.05f, -0.03f}, {1.5f, 1.15f, 0.03f}, pal::metalDark);  // rail
        for (int i = -3; i <= 3; ++i)
            box(f, {i * 0.45f - 0.03f, 0.0f, -0.03f}, {i * 0.45f + 0.03f, 1.15f, 0.03f},
                pal::rust);
    }

    // Kiosk: little booth with a roof and a lit window.
    {
        Mesh& k = M(MeshId::StreetKiosk);
        box(k, {-0.6f, 0.0f, -0.5f}, {0.6f, 1.4f, 0.5f}, pal::rust);
        box(k, {-0.7f, 1.4f, -0.6f}, {0.7f, 1.55f, 0.6f}, pal::rustDark);  // roof
        box(k, {-0.45f, 0.5f, -0.51f}, {0.45f, 1.1f, -0.49f}, pal::dimYellow);  // window
    }

    // Street lamp: post + arm + lit head.
    {
        Mesh& l = M(MeshId::StreetLamp);
        cyl(l, 0.0f, 0.0f, 0.06f, 0.0f, 3.0f, 6, pal::metalDark);
        box(l, {0.0f, 2.9f, -0.02f}, {0.5f, 3.0f, 0.02f}, pal::metalDark);   // arm
        box(l, {0.4f, 2.7f, -0.1f}, {0.6f, 2.9f, 0.1f}, pal::lampGlow);      // head
    }

    // Gate: two posts + a crossbar (factory checkpoint).
    {
        Mesh& g = M(MeshId::Gate);
        box(g, {-1.2f, 0.0f, -0.08f}, {-1.0f, 2.4f, 0.08f}, pal::rust);
        box(g, {1.0f, 0.0f, -0.08f}, {1.2f, 2.4f, 0.08f}, pal::rust);
        box(g, {-1.2f, 2.2f, -0.08f}, {1.2f, 2.4f, 0.08f}, pal::rustDark);
        box(g, {-0.1f, 2.4f, -0.05f}, {0.1f, 2.7f, 0.05f}, pal::dimYellow);  // marker light
    }

    // ---------------- World: Factory ----------------
    box(M(MeshId::FactoryFloor), {-3.0f, -0.05f, -3.0f}, {3.0f, 0.0f, 3.0f}, pal::concrete * 0.8f);
    T(MeshId::FactoryFloor) = panelTex();

    box(M(MeshId::FactoryWall), {-3.0f, 0.0f, -0.12f}, {3.0f, 4.0f, 0.12f}, pal::rustDark * 1.3f);
    T(MeshId::FactoryWall) = panelTex();

    // Assembly table: heavy metal workbench + legs + a vise nub.
    {
        Mesh& a = M(MeshId::AssemblyTable);
        box(a, {-0.8f, 0.85f, -0.5f}, {0.8f, 0.95f, 0.5f}, pal::metal);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                box(a, {sx * 0.72f - 0.05f, 0.0f, sz * 0.42f - 0.05f},
                    {sx * 0.72f + 0.05f, 0.85f, sz * 0.42f + 0.05f}, pal::metalDark);
        box(a, {-0.15f, 0.95f, -0.1f}, {0.15f, 1.1f, 0.1f}, pal::metalDark);
    }

    // Lamppost socket: a base plate with a hollow-looking rusty stub.
    {
        Mesh& s = M(MeshId::LamppostSocket);
        box(s, {-0.35f, 0.0f, -0.35f}, {0.35f, 0.12f, 0.35f}, pal::metalDark);
        cyl(s, 0.0f, 0.0f, 0.16f, 0.12f, 0.5f, 6, pal::rust);
        cyl(s, 0.0f, 0.0f, 0.1f, 0.12f, 0.55f, 6, pal::black);  // inner shadow
    }

    // Lamppost (assembled pillar): base + tall pole + head + glow.
    {
        Mesh& l = M(MeshId::Lamppost);
        box(l, {-0.3f, 0.0f, -0.3f}, {0.3f, 0.15f, 0.3f}, pal::metalDark);
        cyl(l, 0.0f, 0.0f, 0.12f, 0.15f, 3.4f, 8, pal::metal);
        box(l, {-0.28f, 3.4f, -0.28f}, {0.28f, 3.7f, 0.28f}, pal::metalDark);  // housing
        box(l, {-0.22f, 3.42f, -0.22f}, {0.22f, 3.62f, 0.22f}, pal::lampGlow);  // lamp
    }

    // ---------------- Items / weapons ----------------
    // Brick: reddish box, brick-ish 2:1:1, centred on origin (tumbles in flight).
    boxC(M(MeshId::Brick), {0, 0, 0}, {0.5f, 0.25f, 0.28f}, pal::brick);
    T(MeshId::Brick) = brickTex();

    // Pipe: thin metal cylinder along Y, centred.
    cyl(M(MeshId::Pipe), 0.0f, 0.0f, 0.07f, -0.6f, 0.6f, 6, pal::metal);

    // Pickup: a floating gold diamond over a small base pad.
    box(M(MeshId::Pickup), {-0.18f, 0.0f, -0.18f}, {0.18f, 0.05f, 0.18f}, pal::metalDark);
    octa(M(MeshId::Pickup), {0.0f, 0.35f, 0.0f}, 0.16f, 0.22f, pal::pickupGold);

    // First-person held items (modelled ~1 unit, gameplay scales them to ~0.15).
    // ViewHand: a pale fist.
    {
        Mesh& h = M(MeshId::ViewHand);
        boxC(h, {0.0f, 0.0f, 0.0f}, {0.35f, 0.28f, 0.4f}, pal::skin);          // fist
        boxC(h, {0.0f, -0.35f, 0.1f}, {0.28f, 0.25f, 0.3f}, pal::navy);        // cuff
    }
    // ViewBrick: fist gripping a brick.
    {
        Mesh& v = M(MeshId::ViewBrick);
        boxC(v, {0.0f, 0.15f, 0.0f}, {0.5f, 0.24f, 0.28f}, pal::brick);        // brick
        boxC(v, {0.0f, -0.25f, 0.1f}, {0.34f, 0.26f, 0.36f}, pal::skin);       // fist
        boxC(v, {0.0f, -0.6f, 0.12f}, {0.28f, 0.24f, 0.3f}, pal::navy);        // cuff
        T(MeshId::ViewBrick) = brickTex();
    }
    // ViewPipe: fist gripping a diagonal pipe.
    {
        Mesh& v = M(MeshId::ViewPipe);
        cyl(v, 0.0f, 0.0f, 0.09f, -0.2f, 1.1f, 6, pal::metal);                 // pipe
        boxC(v, {0.0f, -0.3f, 0.05f}, {0.32f, 0.24f, 0.34f}, pal::skin);       // fist
        boxC(v, {0.0f, -0.62f, 0.1f}, {0.27f, 0.22f, 0.29f}, pal::navy);       // cuff
    }

    // ---------------- Actors ----------------
    // Kipuchka: small twitchy blob — lumpy pale body, navy cap, two black eyes.
    {
        Mesh& k = M(MeshId::Kipuchka);
        boxC(k, {0.0f, 0.24f, 0.0f}, {0.26f, 0.24f, 0.24f}, pal::skinPale * 0.9f);  // body
        boxC(k, {0.06f, 0.5f, 0.02f}, {0.18f, 0.14f, 0.16f}, pal::navy);            // lumpy head
        boxC(k, {-0.06f, 0.12f, 0.0f}, {0.12f, 0.12f, 0.14f}, pal::skinPale * 0.8f);// squat foot
        boxC(k, {-0.07f, 0.55f, -0.15f}, {0.03f, 0.03f, 0.02f}, pal::black);        // eye
        boxC(k, {0.1f, 0.55f, -0.15f}, {0.03f, 0.03f, 0.02f}, pal::black);          // eye
    }

    // Midnight Smoker: tall grey figure, dim yellow eyes.
    figure(M(MeshId::Smoker), 2.2f, 0.28f, pal::greyStreet * 0.8f, pal::greyStreet,
           pal::dimYellow);

    // Smoke cloud: a puffy cluster of pale cubes (~1 unit radius). Gameplay tints
    // and scales it; the z-buffer handles overlap (no real alpha needed here).
    {
        Mesh& s = M(MeshId::SmokeCloud);
        const Vec3 puffs[6] = {{0, 0.2f, 0}, {0.5f, 0.3f, 0.2f}, {-0.45f, 0.25f, -0.15f},
                               {0.2f, 0.5f, -0.4f}, {-0.3f, 0.55f, 0.35f}, {0.35f, 0.15f, 0.45f}};
        const float r[6] = {0.5f, 0.35f, 0.38f, 0.3f, 0.28f, 0.32f};
        for (int i = 0; i < 6; ++i)
            boxC(s, puffs[i], {r[i], r[i], r[i]}, pal::smoke);
    }

    // Boss: a larger, broad-shouldered figure with an ominous dim glow.
    {
        Mesh& b = M(MeshId::Boss);
        figure(b, 3.0f, 0.5f, pal::rustDark * 1.2f, pal::black * 2.0f, pal::blood * 1.6f);
        box(b, {-0.75f, 2.2f, -0.3f}, {0.75f, 2.45f, 0.3f}, pal::metalDark);  // shoulder yoke
    }

    // ---------------- Protagonist ----------------
    // Try the disc's real model + paletted texture; cube-fall-back if missing so
    // the build never fails even when the asset isn't next to the executable.
    if (auto obj = loadObj("mppcdiscs/solid/assets/protagonist.obj")) {
        M(MeshId::Protagonist) = std::move(*obj);
        if (auto tex = loadImage("mppcdiscs/solid/assets/protagonist_tex.png"))
            T(MeshId::Protagonist) = std::move(*tex);
    } else {
        // Fallback: a rough blocky protagonist (feet-pivoted) in canon colours —
        // navy jumpsuit, brown coat, pale skin, black hat.
        Mesh& p = M(MeshId::Protagonist);
        figure(p, 1.9f, 0.3f, pal::navy, pal::skin, pal::black);
        box(p, {-0.34f, 0.78f, -0.36f}, {0.34f, 1.55f, 0.36f}, pal::brown);   // coat
        box(p, {-0.28f, 1.8f, -0.28f}, {0.28f, 1.92f, 0.28f}, pal::black);    // hat brim/crown
    }
}

}  // namespace rv_3dmppc
