// The 3dmppc console main loop.
//
// Boots the console's services (window/presenter, input, audio, memory card),
// then runs a disc frame-by-frame against them.
//
//  * Headless branch (unchanged behaviour): no window, fixed-ish step, bounded
//    frame count, silent no-op audio + non-persistent save. Used by CI smoke
//    tests (`--headless --frames N`).
//  * Windowed branch: real SDL3 audio, a file-backed memory card, and gamepad
//    input with a simultaneous keyboard fallback so the template is playable on
//    a plain desktop.
#include "platform/console.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "core/audio.hpp"
#include "core/audio_backend.hpp"
#include "core/framebuffer.hpp"
#include "core/input.hpp"
#include "core/savecard.hpp"
#include "core/savecard_file.hpp"
#include "core/window.hpp"
#include "platform/disc.hpp"

namespace rv_3dmppc {

namespace {

// Open the first connected gamepad, or null if none is present. The caller owns
// the handle and must SDL_CloseGamepad() it.
SDL_Gamepad* openFirstGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = nullptr;
    if (ids && count > 0) pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
    return pad;
}

// Normalize a raw stick axis [-32768, 32767] to [-1, 1] with a dead zone so a
// resting stick reads as exactly zero and the live range is rescaled smoothly.
float axisNorm(Sint16 raw) {
    constexpr float kDeadzone = 0.15f;
    float v = raw / 32767.0f;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    const float mag = std::fabs(v);
    if (mag < kDeadzone) return 0.0f;
    const float sign = v < 0.0f ? -1.0f : 1.0f;
    return sign * (mag - kDeadzone) / (1.0f - kDeadzone);
}

// Sample this frame's input from the keyboard AND the gamepad into one
// InputState. Both sources feed the same state and every Button::update() is
// called exactly once per frame with the combined level, so keyboard and pad
// work simultaneously.
//
// Keyboard (desktop fallback): WASD move, arrows look, Space/J throw,
// K/LShift pipe, E/F interact, Q/Tab context, Enter start.
// Gamepad: left stick move, right stick look, South throw, West pipe,
// East interact, North/shoulders context, Start start.
void sampleInput(InputState& in, SDL_Gamepad* pad) {
    const bool* k = SDL_GetKeyboardState(nullptr);

    // --- movement axis (left stick / WASD), y = +forward ---
    float mx = 0.0f, my = 0.0f;
    if (k[SDL_SCANCODE_W]) my += 1.0f;
    if (k[SDL_SCANCODE_S]) my -= 1.0f;
    if (k[SDL_SCANCODE_D]) mx += 1.0f;
    if (k[SDL_SCANCODE_A]) mx -= 1.0f;

    // --- look axis (right stick / arrows), y = +up ---
    float lx = 0.0f, ly = 0.0f;
    if (k[SDL_SCANCODE_RIGHT]) lx += 1.0f;
    if (k[SDL_SCANCODE_LEFT]) lx -= 1.0f;
    if (k[SDL_SCANCODE_UP]) ly += 1.0f;
    if (k[SDL_SCANCODE_DOWN]) ly -= 1.0f;

    // Gamepad sticks override the keyboard axes when actually engaged. SDL's Y
    // axis is down-positive, so negate it to match our +up / +forward convention.
    if (pad) {
        const float gmx = axisNorm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        const float gmy = -axisNorm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        if (gmx != 0.0f || gmy != 0.0f) {
            mx = gmx;
            my = gmy;
        }
        const float glx = axisNorm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        const float gly = -axisNorm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
        if (glx != 0.0f || gly != 0.0f) {
            lx = glx;
            ly = gly;
        }
    }

    in.moveAxis = {mx, my};
    in.lookAxis = {lx, ly};

    // Helper: gamepad button level (false when no pad is connected).
    auto gp = [&](SDL_GamepadButton b) { return pad && SDL_GetGamepadButton(pad, b); };

    // Update each button exactly once per frame with the OR of both sources.
    in.throwBrick.update(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J] ||
                         gp(SDL_GAMEPAD_BUTTON_SOUTH));
    in.meleePipe.update(k[SDL_SCANCODE_K] || k[SDL_SCANCODE_LSHIFT] ||
                        gp(SDL_GAMEPAD_BUTTON_WEST));
    in.interact.update(k[SDL_SCANCODE_E] || k[SDL_SCANCODE_F] ||
                       gp(SDL_GAMEPAD_BUTTON_EAST));
    in.context.update(k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_TAB] ||
                      gp(SDL_GAMEPAD_BUTTON_NORTH) ||
                      gp(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
                      gp(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    in.start.update(k[SDL_SCANCODE_RETURN] || gp(SDL_GAMEPAD_BUTTON_START));
}

}  // namespace

int Console::run(Disc& disc) {
    rv_Framebuffer fb(kNativeWidth, kNativeHeight);

    // ---- Headless: no window, fixed step, bounded frames, silent. ------------
    // Kept deterministic and side-effect free (no audio device, no save file)
    // so it stays usable as a CI smoke test.
    if (cfg_.headless) {
        NullAudio audio;
        NullSaveCard save;
        DiscServices services{&audio, &save};
        disc.boot(services);

        InputState in;
        const int frames = cfg_.maxFrames > 0 ? cfg_.maxFrames : 60;
        for (int f = 0; f < frames && !disc.finished(); ++f) {
            disc.update(in, 1.0f / 60.0f);
            disc.render(fb);
        }
        std::printf("[console] headless run complete: %d frames of '%s'\n", frames,
                    disc.title());
        return 0;
    }

    // ---- Windowed: present at native res with integer upscale. ---------------
    // The window owns SDL video (and SDL_Quit on teardown); declare it first so
    // it outlives the audio/gamepad resources created below.
    rv_Window window(disc.title(), kNativeWidth, kNativeHeight, cfg_.scale);
    if (!window.ok()) return 1;

    // Gamepad: optional. Keyboard remains a full fallback if none is present.
    SDL_Gamepad* pad = nullptr;
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        pad = openFirstGamepad();
        if (pad) {
            std::printf("[console] gamepad connected: %s\n",
                        SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "unknown");
        }
    } else {
        std::fprintf(stderr, "[console] gamepad init failed: %s\n", SDL_GetError());
    }

    // Real services. makeSdlAudio() never returns null (falls back to NullAudio
    // if no device opens); the save card sits next to the working directory.
    std::unique_ptr<Audio> audio(makeSdlAudio());
    std::unique_ptr<SaveCard> save(makeFileSaveCard("solid.card"));
    DiscServices services{audio.get(), save.get()};
    disc.boot(services);

    InputState in;
    std::uint64_t prev = SDL_GetTicks();
    int frame = 0;
    while (window.pumpEvents()) {
        const std::uint64_t now = SDL_GetTicks();
        float dt = (now - prev) / 1000.0f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;  // clamp huge stalls
        if (cfg_.fixedStep) dt = 1.0f / 60.0f;

        sampleInput(in, pad);

        // Mouse look: turn this frame's accumulated pixel motion into a yaw/pitch
        // delta (radians). SDL's Y is down-positive, so negate it for +up pitch.
        constexpr float kMouseSensitivity = 0.0025f;  // rad per pixel (~0.14 deg)
        float mdx = 0.0f, mdy = 0.0f;
        window.consumeMouseDelta(mdx, mdy);
        in.lookDelta = {mdx * kMouseSensitivity, -mdy * kMouseSensitivity};

        disc.update(in, dt);
        disc.render(fb);
        window.present(fb);

        if (disc.finished()) break;
        if (cfg_.maxFrames > 0 && ++frame >= cfg_.maxFrames) break;
    }

    // Tear down audio/gamepad before the window drops SDL entirely.
    if (pad) SDL_CloseGamepad(pad);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return 0;
}

}  // namespace rv_3dmppc
