// Agent D — Home / Street / Factory greybox: geometry, colliders, triggers,
// pickups, and per-frame exit/pickup logic. Blocky and PSX-legible; all meshes
// are unit cubes the renderer scales, so a box is (center, size) + optional
// collider of the same extent. Instance counts stay modest for the frame budget.
#include "game/areas.hpp"

#include "core/audio.hpp"
#include "game/context.hpp"
#include "game/game_state.hpp"
#include "game/weapons.hpp"

namespace rv_3dmppc {

namespace {

// Push a unit-cube draw scaled to `size` at `center`, and (optionally) an AABB
// collider covering the same box.
void addBox(GameState& gs, MeshId mesh, const Vec3& center, const Vec3& size,
            const Vec3& tint = {1, 1, 1}, bool collide = true) {
    DrawInstance d;
    d.mesh = mesh;
    d.pos = center;
    d.scale = size;
    d.tint = tint;
    gs.world.staticDraws.push_back(d);
    if (collide) {
        AABB b;
        b.min = {center.x - size.x * 0.5f, center.y - size.y * 0.5f,
                 center.z - size.z * 0.5f};
        b.max = {center.x + size.x * 0.5f, center.y + size.y * 0.5f,
                 center.z + size.z * 0.5f};
        gs.world.colliders.push_back(b);
    }
}

// A Brick pickup: a ~1 m walk-in trigger plus its visible marker at `pos`.
void addPickup(GameState& gs, const Vec3& pos, int ammo) {
    AreaWorld::Pickup p;
    p.pos = pos;
    p.ammo = ammo;
    p.active = true;
    const float r = 0.6f;  // trigger half-extent (XZ)
    p.trigger.min = {pos.x - r, 0.0f, pos.z - r};
    p.trigger.max = {pos.x + r, 2.0f, pos.z + r};
    gs.world.pickups.push_back(p);
}

// ---- Home: one cramped 6x6 m Khrushchyovka room --------------------------
void loadHome(GameState& gs, const GameContext& ctx) {
    // Layout constants (metres). Room spans x,z in [-3,3]; walls 2.5 m tall,
    // 0.2 m thick; a 1.2 m doorway gap in the north (+z) wall is the exit.
    const float H = 2.5f, T = 0.2f, wy = 1.25f;

    addBox(gs, MeshId::HomeFloor, {0, -0.05f, 0}, {6, 0.1f, 6}, {0.35f, 0.33f, 0.30f}, false);

    // North wall, split around the doorway (x in [-0.6,0.6]).
    addBox(gs, MeshId::HomeWall, {-1.8f, wy, 3}, {2.4f, H, T});
    addBox(gs, MeshId::HomeWall, {1.8f, wy, 3}, {2.4f, H, T});
    // South / east / west walls (solid).
    addBox(gs, MeshId::HomeWall, {0, wy, -3}, {6, H, T});
    addBox(gs, MeshId::HomeWall, {3, wy, 0}, {T, H, 6});
    addBox(gs, MeshId::HomeWall, {-3, wy, 0}, {T, H, 6});

    // Window onto the bleak courtyard (west wall; visual only).
    addBox(gs, MeshId::HomeWindow, {-2.9f, 1.6f, 0}, {0.1f, 1.2f, 1.6f},
           {0.5f, 0.55f, 0.7f}, false);
    // Furniture (colliders): bed, table, and the mood-flipping radio on it.
    addBox(gs, MeshId::HomeBed, {-2.0f, 0.3f, -2.0f}, {1.6f, 0.6f, 2.2f});
    addBox(gs, MeshId::HomeTable, {2.0f, 0.5f, -1.4f}, {1.2f, 1.0f, 0.9f});
    addBox(gs, MeshId::HomeRadio, {2.0f, 1.15f, -1.4f}, {0.5f, 0.3f, 0.4f},
           {0.8f, 0.7f, 0.5f}, false);

    // Visible progression: one clutter prop per accumulated Home mutation,
    // scattered deterministically. Capped so the room never overfills.
    const int n = gs.loop.homeMutations;
    for (int i = 0; i < n && i < 8; ++i) {
        const float x = ctx.rng.range(-2.6f, 2.6f);
        const float z = ctx.rng.range(-2.6f, 2.6f);
        const float s = ctx.rng.range(0.3f, 0.7f);
        addBox(gs, MeshId::HomeProp, {x, s * 0.5f, z}, {s, s, s},
               {0.75f, 0.65f, 0.85f}, false);
    }

    // A couple of spare bricks so the player is never hard-gated.
    addPickup(gs, {-1.4f, 0.4f, 1.4f}, 2);
    addPickup(gs, {1.4f, 0.4f, 0.8f}, 1);

    // Spawn just inside, facing the north doorway (+z is yaw 0).
    gs.world.playerSpawn = {0, 1.6f, 0.5f};
    gs.world.playerSpawnYaw = 0.0f;
    // Exit trigger at the doorway; armed immediately.
    gs.world.exitTrigger.min = {-0.6f, 0, 2.7f};
    gs.world.exitTrigger.max = {0.6f, 3, 3.3f};
    gs.world.exitArmed = true;

    if (ctx.audio) ctx.audio->setMood(Audio::Mood::Home);
}

// ---- Street: a straight modular stretch ending at the factory gate --------
void loadStreet(GameState& gs, const GameContext& ctx) {
    // Layout: `numChunks` road segments of `chunkLen` along +z; road x in
    // [-3,3]; facades at x=±4; hard outer walls at x=±5; gate at the far end.
    const int numChunks = 4;
    const float chunkLen = 10.0f;
    const float totalLen = numChunks * chunkLen;  // 40 m
    const float fy = 1.75f, fh = 3.5f;            // facade centre-y / height

    for (int c = 0; c < numChunks; ++c) {
        const float zc = c * chunkLen + chunkLen * 0.5f;
        addBox(gs, MeshId::StreetRoad, {0, -0.05f, zc}, {6, 0.1f, chunkLen},
               {0.28f, 0.28f, 0.30f}, false);
        // Alleys: skip one facade on chunks 1 and 2 to open a side passage.
        const bool leftAlley = (c == 1);   // opens toward +x
        const bool rightAlley = (c == 2);  // opens toward -x
        if (!leftAlley)
            addBox(gs, MeshId::StreetFacade, {4, fy, zc}, {1, fh, chunkLen});
        if (!rightAlley)
            addBox(gs, MeshId::StreetFacade, {-4, fy, zc}, {1, fh, chunkLen});
        // A lamp at each seam (visual only).
        addBox(gs, MeshId::StreetLamp, {3.2f, 2.4f, c * chunkLen},
               {0.15f, 4.8f, 0.15f}, {0.9f, 0.9f, 0.75f}, false);
        // Chunk-indexed spawn candidates in the clear lanes.
        gs.world.spawnPoints.push_back({2.0f, 0.0f, zc});
        gs.world.spawnPoints.push_back({-2.0f, 0.0f, zc});
    }
    // Extra spawn candidates deep in the two alleys.
    gs.world.spawnPoints.push_back({6.0f, 0.0f, 15.0f});
    gs.world.spawnPoints.push_back({-6.0f, 0.0f, 25.0f});

    // Hard outer bounds (cover the alley gaps) + a wall behind the spawn.
    addBox(gs, MeshId::StreetFacade, {5, fy, totalLen * 0.5f}, {0.5f, fh, totalLen},
           {0.55f, 0.55f, 0.58f});
    addBox(gs, MeshId::StreetFacade, {-5, fy, totalLen * 0.5f}, {0.5f, fh, totalLen},
           {0.55f, 0.55f, 0.58f});
    addBox(gs, MeshId::StreetFacade, {0, fy, -1.0f}, {12, fh, 1},
           {0.55f, 0.55f, 0.58f});

    // Kiosks (colliders) for cover / silhouette.
    addBox(gs, MeshId::StreetKiosk, {2.4f, 0.9f, 12.0f}, {1.4f, 1.8f, 1.4f});
    addBox(gs, MeshId::StreetKiosk, {-2.4f, 0.9f, 22.0f}, {1.4f, 1.8f, 1.4f});
    // A low fence partially gating the -x alley.
    addBox(gs, MeshId::StreetFence, {-4.6f, 0.5f, 23.0f}, {1.6f, 1.0f, 0.2f});

    // Scattered spare bricks (one tucked in the +x alley).
    addPickup(gs, {1.6f, 0.4f, 6.0f}, 1);
    addPickup(gs, {5.6f, 0.4f, 15.0f}, 2);
    addPickup(gs, {-1.8f, 0.4f, 30.0f}, 1);

    // Clearly-lit checkpoint gate at the far end (visual only; walk through it).
    const float zGate = totalLen;
    addBox(gs, MeshId::Gate, {0, 1.9f, zGate}, {6, 3.8f, 0.6f}, {1.0f, 1.0f, 0.8f}, false);
    addBox(gs, MeshId::StreetLamp, {2.6f, 2.4f, zGate}, {0.2f, 4.8f, 0.2f},
           {1.0f, 1.0f, 0.85f}, false);
    addBox(gs, MeshId::StreetLamp, {-2.6f, 2.4f, zGate}, {0.2f, 4.8f, 0.2f},
           {1.0f, 1.0f, 0.85f}, false);

    // Reaching the gate is the objective — arm on load, place it far.
    gs.world.exitTrigger.min = {-3, 0, zGate - 1.0f};
    gs.world.exitTrigger.max = {3, 3, zGate + 1.0f};
    gs.world.exitArmed = true;

    gs.world.playerSpawn = {0, 1.6f, 2.0f};
    gs.world.playerSpawnYaw = 0.0f;

    if (ctx.audio) ctx.audio->setMood(Audio::Mood::Street);
}

// ---- Factory: a hangar hall with a central ritual socket ------------------
void loadFactory(GameState& gs, const GameContext& ctx) {
    // Hall spans x in [-8,8], z in [0,16]; walls 4 m tall; entrance doorway
    // (x in [-1,1]) in the south (z=0) wall; open central floor for the arena.
    const float wy = 2.0f, T = 0.3f;

    addBox(gs, MeshId::FactoryFloor, {0, -0.05f, 8}, {16, 0.1f, 16},
           {0.22f, 0.20f, 0.18f}, false);
    // North / east / west walls.
    addBox(gs, MeshId::FactoryWall, {0, wy, 16}, {16, 4, T});
    addBox(gs, MeshId::FactoryWall, {8, wy, 8}, {T, 4, 16});
    addBox(gs, MeshId::FactoryWall, {-8, wy, 8}, {T, 4, 16});
    // South wall split around the entrance doorway.
    addBox(gs, MeshId::FactoryWall, {-4.5f, wy, 0}, {7, 4, T});
    addBox(gs, MeshId::FactoryWall, {4.5f, wy, 0}, {7, 4, T});

    // Assembly tables around the perimeter (leave the centre open).
    addBox(gs, MeshId::AssemblyTable, {-5.0f, 0.5f, 4.0f}, {2.4f, 1.0f, 1.0f});
    addBox(gs, MeshId::AssemblyTable, {5.0f, 0.5f, 4.0f}, {2.4f, 1.0f, 1.0f});
    addBox(gs, MeshId::AssemblyTable, {-5.0f, 0.5f, 12.0f}, {2.4f, 1.0f, 1.0f});
    addBox(gs, MeshId::AssemblyTable, {5.0f, 0.5f, 12.0f}, {2.4f, 1.0f, 1.0f});

    // The lamppost socket at the arena centre (ritual system draws it).
    gs.world.socketPos = {0, 0, 8};
    gs.world.hasSocket = true;

    // Encounter spawn points ring the arena.
    gs.world.spawnPoints.push_back({-6.0f, 0.0f, 5.0f});
    gs.world.spawnPoints.push_back({6.0f, 0.0f, 5.0f});
    gs.world.spawnPoints.push_back({-6.0f, 0.0f, 11.0f});
    gs.world.spawnPoints.push_back({6.0f, 0.0f, 11.0f});
    gs.world.spawnPoints.push_back({0.0f, 0.0f, 14.0f});

    // Return trigger sits at the entrance; unlocked only when the ritual is done.
    gs.world.exitTrigger.min = {-1.2f, 0, -0.5f};
    gs.world.exitTrigger.max = {1.2f, 3, 1.2f};
    gs.world.exitArmed = false;

    gs.world.playerSpawn = {0, 1.6f, 2.0f};
    gs.world.playerSpawnYaw = 0.0f;

    if (ctx.audio) ctx.audio->setMood(Audio::Mood::Factory);
}

// Prompt shown while standing in an armed exit trigger.
const char* exitPrompt(Area a) {
    switch (a) {
        case Area::Home:    return "Leave the apartment";
        case Area::Street:  return "Enter the factory";
        case Area::Factory: return "Return home";
    }
    return nullptr;
}

}  // namespace

void loadArea(GameState& gs, const GameContext& ctx, Area area) {
    gs.world = AreaWorld{};
    gs.world.current = area;
    switch (area) {
        case Area::Home:    loadHome(gs, ctx); break;
        case Area::Street:  loadStreet(gs, ctx); break;
        case Area::Factory: loadFactory(gs, ctx); break;
    }
    // Hand this area's candidate spawn points to the spawner to read.
    gs.spawner.points = gs.world.spawnPoints;
}

void updateAreas(GameState& gs, const GameContext& ctx, float dt) {
    (void)dt;
    const Vec3 pp = gs.player.pos;

    // Pickups: standing on an active one grants brick ammo, clicks, retires it.
    for (auto& pk : gs.world.pickups) {
        if (!pk.active) continue;
        if (pk.trigger.containsXZ(pp)) {
            addBrickAmmo(gs, pk.ammo);
            if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::UiClick);
            pk.active = false;
        }
    }

    // Exit detection: latch atExit only when the trigger is both entered & armed.
    const bool inTrigger = gs.world.exitTrigger.containsXZ(pp);
    gs.world.atExit = inTrigger && gs.world.exitArmed;
    if (inTrigger && gs.world.exitArmed && !gs.prompt)
        gs.prompt = exitPrompt(gs.world.current);
}

bool playerAtExit(const GameState& gs) { return gs.world.atExit; }

void collectAreaDraws(const GameState& gs, DrawList& out) {
    for (const auto& d : gs.world.staticDraws) out.push_back(d);
    // Active pickups draw as floating markers (removed once taken).
    for (const auto& pk : gs.world.pickups) {
        if (!pk.active) continue;
        DrawInstance d;
        d.mesh = MeshId::Pickup;
        d.pos = pk.pos;
        d.scale = {0.4f, 0.4f, 0.4f};
        out.push_back(d);
    }
}

}  // namespace rv_3dmppc
