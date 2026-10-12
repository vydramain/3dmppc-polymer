// Browser entry point. A browser has no command line, so the console gets a fixed one.
// The disc is embedded in the page at /disc.mppcdisc; the memory card lives in /save, an IDBFS
// mount (IndexedDB) that survives a page reload. The mount is set up in src/web/rv_web_pre.js.
#include <emscripten/emscripten.h>

#include <iterator>

#include "rv_pboot/rv_pboot.hpp"

namespace
{

// Fixed command line: the disc is the positional argument, -m points at the card.
constexpr const char *kProgramName = "3dmppc";
constexpr const char *kMemcardOption = "-m";
constexpr const char *kMemcardPath = "/save/memcard.mppccard";
constexpr const char *kDiscPath = "/disc.mppcdisc";

// IDBFS is written back to IndexedDB this often; a crash loses at most this much.
constexpr int kFlushIntervalMs = 1000;

} // namespace

int main()
{
    // getopt permutes the pointers in argv but never writes the strings, so literals are safe as char *.
    char *argv[] = {
        const_cast<char *>(kProgramName),
        const_cast<char *>(kMemcardOption),
        const_cast<char *>(kMemcardPath),
        const_cast<char *>(kDiscPath),
    };
    const int argc = static_cast<int>(std::size(argv));

    // Best effort: the pagehide write is asynchronous, so a closing tab loses at most about
    // one flush interval plus one in-flight flush. Module.rvSaveFlush is in src/web/rv_web_pre.js.
    EM_ASM(
        {
            setInterval(Module.rvSaveFlush, $0);
            window.addEventListener('pagehide', Module.rvSaveFlush);
        },
        kFlushIntervalMs);

    // Does not return once the run starts (the frame driver takes over the browser loop).
    return rv_3dmppc::rv_pboot_run(argc, argv);
}
