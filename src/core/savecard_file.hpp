// Factory for the file-backed memory card. Declared here so the console
// (platform/console.cpp) can construct one without depending on the concrete
// type — the implementation lives in core/savecard.cpp (Agent A).
#pragma once

namespace rv_3dmppc {

class SaveCard;

// Create a file-backed 128 KB memory card that persists to `path`. The caller
// owns the returned object and must `delete` it. Never returns null — the
// returned card is always usable (an empty/missing file just reads as a fresh
// card). See core/savecard.hpp for the interface contract.
SaveCard* makeFileSaveCard(const char* path);

}  // namespace rv_3dmppc
