#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_bare
{

// One period of the demo's camera loop: 4320 frames = 72 s at 1/60 s per frame.
// The camera of a frame is a pure function of its number, so the period repeats exactly.
constexpr int64_t RV_ROUTE_PERIOD_FRAMES = 4320;

// One cam_NN key of the route: where the camera stands and which way it looks, in Q20.12.
struct rv_route_key {
    int32_t index; // NN of cam_NN: the key order on the route
    rv_pdklib::fx_vec3 eye;
    rv_pdklib::fx_vec3 dir; // unit length, see rv_route_unit
};

// v scaled to unit length in Q20.12; a zero-length v gives +z rather than a division by zero.
// Error: the result leaves Q20.12; out is untouched.
int rv_route_unit(rv_pdklib::fx_vec3 v, rv_pdklib::fx_vec3 &out);

// The route keys of bare_cameras(), sorted by index: eye = world t, dir = unit z column of world r.
// Error: no camera (RV_ERR_NOENT), or dir leaves Q20.12 (RV_ERR_INVAL); out is untouched.
int rv_route_keys(std::vector<rv_route_key> &out);

// Eye of frame `frame` in Q20.12, on the same spline of eyes as rv_route_camera_at.
// Error: keys empty (RV_ERR_NOENT), or the Q20.12 math leaves its range (RV_ERR_INVAL); out is untouched.
int rv_route_eye_at(const std::vector<rv_route_key> &keys, int64_t frame, rv_pdklib::fx_vec3 &out);

// Camera of frame `frame` (any value, negative too; taken modulo the period) for a screen of screen_height pixels.
// keys must be sorted by index. Error: keys empty (RV_ERR_NOENT); screen_height not a positive int32, the Q20.12
// math leaves its range, or rv_gte_camera_make refuses (RV_ERR_INVAL); out is untouched. Not logged: a frame
// repeats it, so the disc logs the first one.
int rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame, int64_t screen_height, rv_cg_camera &out);

// Cells of the board's text ("ОСТАЛОСЬ: 5"), counted from 0; a cell may be the space between colon and digit.
constexpr int RV_BOARD_CELL_COUNT = 11;

// True when cell i (0 <= i < RV_BOARD_CELL_COUNT) is lit in frame `frame` (frame >= 0: a negative frame indexes
// before the table; callers pass frame % RV_ROUTE_PERIOD_FRAMES): the flicker table decides it, the space cell never
// lights. The sound takes the lit set from here too, so both agree.
bool rv_board_cell_lit(int i, int64_t frame);

// Burn window of one lamp: the frames of the route period in which it burns, [on_frame, off_frame).
// Both 0 means it never burns.
struct rv_lamp_schedule {
    int64_t on_frame = 0;
    int64_t off_frame = 0;
};

// Schedule of each bare_lamps() entry, in its order, from keys (sorted by index) and the lamp's base.
// A lamp the route does not pass in both halves of the period, or whose offset or route math leaves Q20.12,
// is warned once and stays dark. No keys: every lamp is warned and stays dark.
void rv_lamps_schedule(const std::vector<rv_route_key> &keys, std::vector<rv_lamp_schedule> &out);

// True when the lamp burns at frame's place in the period (frame % RV_ROUTE_PERIOD_FRAMES): on_frame <= place < off_frame.
bool rv_lamp_lit(const rv_lamp_schedule &lamp, int64_t frame);

// One named sample the disc has asked for. The address is the drive's and is valid until the disc unloads.
struct rv_bare_sound_asset {
    std::string name;
    int64_t addr = 0;
    bool resident = false;
    // The first failure is logged; a later failure of the same asset is silent.
    bool logged = false;
};

// Owned by the bare sound state. A disc's samples are few, so a linear search by name is enough.
struct rv_bare_sound_state {
    std::vector<rv_bare_sound_asset> assets;
};

// Makes `name` resident and returns its sound RAM address in `addr` (set only on RV_OK). The first call reads and
// uploads through the drive; later calls return the same address without a drive call. A failure returns its rv_err:
// a null cd or a drive failure is logged once per asset; a null name is a programming error, logged on every call.
[[nodiscard]] int rv_bare_sound_resident(rv_cd *cd, rv_bare_sound_state &state, const char *name, int64_t &addr);

// Sets up the voice from conf (sample address, loop, envelope, volumes); conf is passed through unchanged.
// Unlogged: a voice call is retried every frame while it fails, so the caller logs the first failure.
[[nodiscard]] int rv_bare_sound_voice_setup(rv_ca *ca, const rv_voice_conf &conf);

// Starts the voices in `voice` (a mask), as set up by rv_bare_sound_voice_setup.
[[nodiscard]] int rv_bare_sound_voice_play(rv_ca *ca, int64_t voice);

// Stops the voices in `voice` (a mask).
[[nodiscard]] int rv_bare_sound_voice_stop(rv_ca *ca, int64_t voice);

// The place the camera is in: the factory (workshop), the street, the flat (apartment).
enum class rv_sound_area : int {
    workshop = 0,
    street = 1,
    flat = 2,
};

// Music slots in sound RAM: a bar plays from one slot while the next bar is written into the other.
constexpr int RV_SOUND_SLOTS = 2;

// The factory loop: it runs while the camera is in the workshop.
constexpr int RV_SOUND_LOOP_COUNT = 1;

// Full level of a voice's sustain and volume: the sound module's maximum, shared by the music and the loops.
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Music, clack and loop state. Filled once by rv_sound_init; frame = -1 before the first frame.
struct rv_sound_state {
    // The last frame the sound played for, and its area and bar; the bar counts from the area span's first frame.
    int64_t frame = -1;
    rv_sound_area area = rv_sound_area::workshop;
    int64_t bar = -1;
    // Per frame of the period: its area, and the first frame of its area span (cyclic). Empty until built.
    std::vector<rv_sound_area> areas;
    std::vector<int64_t> span_start;
    // Per frame of the period: the cumulative horizontal distance of the eye from frame 0, metres in Q20.12
    // (fx raw, 4096 per metre). Empty until built.
    std::vector<rv_pdklib::fx> distance;
    // Per music slot: sound RAM address, bar asset in it (nullptr when unknown).
    int64_t slot_addr[RV_SOUND_SLOTS] = {};
    const char *slot_name[RV_SOUND_SLOTS] = {};
    // The music chain: the voice reads the slot before music_slot and goes on into music_slot, where its mark is
    // set. music_bar is the bar in music_slot and music_name its asset; music_on is set once a bar is played,
    // music_armed once the repeat and mark on music_slot are set.
    bool music_on = false;
    bool music_armed = false;
    int music_slot = 0;
    int64_t music_bar = 0;
    const char *music_name = nullptr;
    // Per factory loop: whether its voice was started and not stopped since.
    bool loop_armed[RV_SOUND_LOOP_COUNT] = {};
    // One bar read from the disc, slot-sized.
    std::vector<uint8_t> scratch;
    // The clack and loop samples, and the step samples once rv_steps_update runs, made resident by the sound service.
    rv_bare_sound_state sound;
    // The asset of the last failure, for the caller's log.
    const char *error_asset = "";
};

// Measures the music on the disc, allocates the music slots, tabulates the area and span of every frame of the
// period, and tabulates the footstep distance (rv_steps_init) from keys (non-empty, sorted by index). Called once;
// returns RV_OK or the first drive, ca, areas, or step-path error, with state.error_asset naming the asset,
// "sound areas" or "step path". The route fails with RV_ERR_NOENT (no keys) or RV_ERR_INVAL (the Q20.12 math
// leaves its range), for both the areas and the steps.
int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_state &state);

// Plays what frame `frame` (in [0, RV_ROUTE_PERIOD_FRAMES)) needs after state.frame: the music of its area
// (rv_music_update), the board clack in the workshop when the lit letters differ from state.frame's, and
// only when the scene has the board (board_present). The two factory loops run while the frame is in the
// workshop. The same frame again (a pause, a repeated render) plays nothing. The first frame fills the
// slots and waits for the next bar start when it is mid-bar.
// Needs rv_sound_init first. Returns RV_OK or the drive or ca error, with state.error_asset naming the asset.
int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, rv_sound_state &state);

// Records `asset` as the failure's name for the caller's log and returns `code`.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code);

// Sets up and starts conf.voice.
int64_t play_voice(rv_pdko *pdk, const rv_voice_conf &conf);

// Music. rv_music_init allocates the music slots and measures the bars; rv_sound_init calls it first.
// rv_music_update starts the chain at an area change and at a bar start while it is not running, moves it on
// at each mark, and on success records the frame's area and bar; rv_sound_update calls it. Only rv_music_update
// needs the sound tables of rv_sound_init (areas, span_start).
int64_t rv_music_init(rv_pdko *pdk, rv_sound_state &state);
int64_t rv_music_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state);

// Footsteps: one per 0.95 m of horizontal eye path, on the floor of the frame's area, A and B alternating.
// rv_steps_init tabulates the distance (rv_sound_init calls it) and returns RV_OK or the route or fx error, with
// state.error_asset set; rv_steps_update would play the step of a new frame, but rv_sound_update does not call it: the
// call is commented out, as in the pdkkit sample, so no step sounds. Both need state.areas, which rv_sound_init fills
// first.
int64_t rv_steps_init(const std::vector<rv_route_key> &keys, rv_sound_state &state);
int64_t rv_steps_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state);

// Draw port, in example-cpp-bare-draw.cpp. rv_draw_camera sets the camera for the puts after it. rv_draw_put puts one
// model with its world matrix: a decal takes the decal depth window, anything else the world's. Both return RV_OK or
// the drive's code; the caller logs the first failure.
int rv_draw_camera(rv_pdko *pdk, const rv_cg_camera &camera);
int rv_draw_put(rv_pdko *pdk, const char *model, const rv_pdklib::rv_gte_matrix &world, rv_color tint, bool decal);

} // namespace example_cpp_bare
