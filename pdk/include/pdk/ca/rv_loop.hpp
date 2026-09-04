#pragma once

namespace rv_pdk
{

enum class rv_loop {
	none = 0, // play once, then the voice goes silent (one-shot)
	forever,  // repeat the whole sample indefinitely
	// sustain,  // DEFERRED: loop until voice_stop, then run the release phase
};

} // namespace rv_pdk
