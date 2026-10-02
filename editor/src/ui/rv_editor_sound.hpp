#pragma once

#include <filesystem>
#include <string>

namespace rv_editor
{

// The editor's one-sound-at-a-time player, for Assets' preview (P5.2). Opens
// SDL's audio subsystem lazily on the first play; a failure is text, never
// fatal. A .wav plays in its own format; a .pcm is the console's raw S16LE
// mono 44100 Hz. Playing another file, or rv_editor_sound_stop, replaces it.

// Starts `file` playing, replacing whatever played before. False with
// `error` set on failure (bad subsystem, unreadable file, unknown format).
bool rv_editor_sound_play(const std::filesystem::path &file, std::string &error);

// Stops whatever plays; harmless when nothing does.
void rv_editor_sound_stop();

// True from a successful play until its stream drains or it is stopped.
bool rv_editor_sound_playing();

// The path passed to the last successful play, valid while it still plays.
const std::filesystem::path &rv_editor_sound_path();

// Releases the audio subsystem if it was opened. Called once from main's
// shutdown so no stream outlives the window.
void rv_editor_sound_shutdown();

} // namespace rv_editor
