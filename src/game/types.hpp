// Shared value types for the Solidmaid disc: how gameplay describes what to draw
// (the draw list) and the tiny physics primitives systems exchange.
//
// SHARED CONTRACT — owned by the orchestrator. Gameplay systems (B/C/D) *push*
// DrawInstances describing their visuals; the renderer (E) *consumes* the list
// and owns the actual meshes behind each MeshId. This keeps geometry ownership
// (gameplay) separate from mesh ownership (renderer), so agents never edit each
// other's files. Add MeshId values by appending before Count.
#pragma once

#include <cstdint>
#include <vector>

#include "math/math.hpp"

namespace rv_3dmppc {

// Logical mesh handle. The renderer (Agent E) maps each to a procedural mesh +
// texture. Gameplay only ever refers to these ids, never to concrete meshes.
enum class MeshId : int {
    None = 0,
    // World — Home
    HomeFloor,
    HomeWall,
    HomeBed,
    HomeRadio,
    HomeTable,
    HomeWindow,
    HomeProp,     // generic mutation prop (poster/lamp/clutter)
    // World — Street
    StreetRoad,
    StreetFacade,
    StreetFence,
    StreetKiosk,
    StreetLamp,
    Gate,         // checkpoint gate to the factory
    // World — Factory
    FactoryFloor,
    FactoryWall,
    AssemblyTable,
    LamppostSocket,
    Lamppost,     // fully assembled pillar
    // Items / weapons
    Brick,
    Pipe,
    Pickup,       // world pickup marker
    ViewBrick,    // first-person held brick
    ViewPipe,     // first-person held pipe
    ViewHand,     // empty first-person hand
    // Actors
    Kipuchka,
    Smoker,
    SmokeCloud,   // expanding AoE billboard-ish puff
    Boss,
    Protagonist,  // full silhouette (summary/reflection)
    Count
};

// One thing to draw this frame: a mesh at a transform, tinted. yaw/pitch/roll in
// radians; scale is per-axis. tint multiplies the mesh's vertex/texel color
// (use it for telegraphs, corruption, low-HP mood).
struct DrawInstance {
    MeshId mesh = MeshId::None;
    Vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    Vec3 scale{1, 1, 1};
    Vec3 tint{1, 1, 1};
};

using DrawList = std::vector<DrawInstance>;

// Axis-aligned box — the template's one collision primitive (walls, props,
// triggers). Kept trivially cheap for the software rasterizer's frame budget.
struct AABB {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    bool containsXZ(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.z >= min.z && p.z <= max.z;
    }
    Vec3 center() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f,
                                  (min.z + max.z) * 0.5f}; }
};

// Thrown brick (and any future arcing projectile). Owned in GameState; spawned
// by weapons (Agent B), integrated + resolved against enemies by combat (C).
struct Projectile {
    Vec3 pos{0, 0, 0};
    Vec3 vel{0, 0, 0};
    float life = 0.0f;   // seconds remaining before it despawns
    bool active = false;
    int kind = 0;        // 0 = brick
    bool hasHit = false; // set once it deals damage, so it hits once
};

}  // namespace rv_3dmppc
