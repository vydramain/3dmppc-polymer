// File-backed 128 KB memory card. Reads the saved blob on demand and persists
// writes atomically (write to a temp file, then rename over the target) so a
// crash mid-write can never corrupt an existing save. Degrades to "fresh card"
// (read() returns false) when the file is missing or too short.
//
// The header provides NullSaveCard for headless smoke tests; this file adds the
// real persistent card plus the makeFileSaveCard() factory the console calls.
#include "core/savecard.hpp"

#include <cstdio>
#include <string>

#include "core/savecard_file.hpp"

namespace rv_3dmppc {

namespace {

// Concrete card backed by a single binary file on disk.
class FileSaveCard final : public SaveCard {
public:
    explicit FileSaveCard(const char* path) : path_(path ? path : "solid.card") {}

    // Load up to `n` bytes from the file into `dst`. Returns false (leaving
    // `dst` untouched) when there is no valid save yet — i.e. the file is
    // missing or holds fewer than `n` bytes — so the caller treats it as fresh.
    bool read(void* dst, std::size_t n) override {
        if (!dst || n == 0 || n > kCapacity) return false;

        std::FILE* f = std::fopen(path_.c_str(), "rb");
        if (!f) return false;  // no save yet → fresh card

        // Require at least `n` bytes present; a short file is treated as fresh.
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return false;
        }
        const long size = std::ftell(f);
        if (size < 0 || static_cast<std::size_t>(size) < n) {
            std::fclose(f);
            return false;
        }

        std::rewind(f);
        const std::size_t got = std::fread(dst, 1, n, f);
        std::fclose(f);
        return got == n;
    }

    // Persist `n` bytes atomically. Writes to `<path>.tmp`, flushes, then
    // renames over the real file. Returns false on any I/O failure (and cleans
    // up the temp file so a failed write leaves no partial junk behind).
    bool write(const void* src, std::size_t n) override {
        if (!src || n > kCapacity) return false;

        const std::string tmp = path_ + ".tmp";
        std::FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return false;

        const std::size_t put = std::fwrite(src, 1, n, f);
        // Force the bytes out before we commit the rename.
        const bool flushed = std::fflush(f) == 0;
        const bool closed = std::fclose(f) == 0;
        if (put != n || !flushed || !closed) {
            std::remove(tmp.c_str());
            return false;
        }

        // Atomic commit: rename replaces the destination in a single step.
        if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
        return true;
    }

private:
    std::string path_;
};

}  // namespace

SaveCard* makeFileSaveCard(const char* path) { return new FileSaveCard(path); }

}  // namespace rv_3dmppc
