// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 10 — запись zip-архива .mppcdisc (только метод store).
// ──────────────────────────────────────────────────────────────────────────────
//
// Writes the `.mppcdisc` container: a plain zip using the STORE method only —
// no compression, ever.
//
// WHY STORED AND NOT DEFLATED. The console has to read this archive, and a
// stored entry is read with one seek and one read straight out of the central
// directory's offset. A deflated one would put zlib inside the console — a
// dependency the machine otherwise does not have, in the one component that
// must stay small and auditable. The medium is also the model here: a CD-ROM
// held its data as it was, and the console's whole texture pipeline already
// assumes bytes it can use directly. If compression ever earns its place, it
// arrives as a bump of the container version, not as a change to this one.
//
// The reader on the other side is src/rv_pconsole/cd/rv_pczip.*.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rv_pdktools {

class rv_zipwriter {
   public:
    explicit rv_zipwriter(const std::string& path);
    ~rv_zipwriter();

    rv_zipwriter(const rv_zipwriter&) = delete;
    rv_zipwriter& operator=(const rv_zipwriter&) = delete;

    // True when the output file opened. Check before adding anything.
    bool ok() const;

    // Append one entry. `name` is the name the console will see — for assets
    // that means a FLAT name with no path separators, because rv_cd resolves a
    // resource by name and its contract forbids separators outright.
    //
    // Returns false and sets `error` on an I/O failure or a duplicate name.
    bool add(const std::string& name, const void* data, std::size_t size, std::string& error);

    // Same, reading the bytes from a file on disk.
    bool add_file(const std::string& name, const std::string& source_path, std::string& error);

    // Write the central directory and close. MUST be called: an archive without
    // it is not an archive, and no reader will open it. Returns false on I/O
    // failure, in which case the partial file is removed rather than left
    // looking like a disc.
    bool finish(std::string& error);

    // Names added so far, in order — the burner prints them and checks for the
    // flat-name collisions the console could not diagnose.
    const std::vector<std::string>& entries() const;

   private:
    struct rv_zipwriter_impl;
    rv_zipwriter_impl* impl_;
};

}  // namespace rv_pdktools
