#include "rv_burner_inspect_runner.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

#include "pdklib/rv_stdio.hpp"

#include "rv_burner_print.hpp"
#include "rv_burner_zip/rv_burner_zipread.hpp"

namespace rv_pdktools
{

// The entry the console reads first. An archive without it is not a disc,
// whatever else it contains.
static constexpr const char *k_entry_manifest = "disc.toml";

} // namespace rv_pdktools

int rv_pdktools::rv_burner_inspect_run(const rv_burner_options &options)
{
    std::string error;

    zip_archive archive;
    if (!zip_open(options.operand, archive, error)) {
        rv_burner_print_error(options.operand + ": " + error);
        return 1;
    }

    // --- the manifest, verbatim ---
    //
    // Printed as bytes rather than parsed and re-rendered: the question this
    // command answers is what the ARCHIVE carries, and re-rendering would show
    // what this burner would have written instead.
    rv_pdklib::rv_fprintf(stdout, "[manifest]\n");

    const zip_read_entry *manifest = zip_find(archive, k_entry_manifest);
    if (manifest == nullptr) {
        rv_burner_print_warning(
            options.operand + " has no " + k_entry_manifest + "; the console would refuse it");
    } else {
        std::string text;
        if (!zip_entry_bytes(archive, *manifest, text, error)) {
            rv_burner_print_error(error);
            return 1;
        }
        std::fwrite(text.data(), 1, text.size(), stdout);
        if (!text.empty() && text.back() != '\n') {
            std::fputc('\n', stdout);
        }
    }

    // --- one line per entry ---

    rv_pdklib::rv_fprintf(stdout, "[entries]\n");

    int64_t total = 0;
    for (const zip_read_entry &entry : archive.entries) {
        rv_pdklib::rv_fprintf(stdout, "%s\t%lld\n", entry.name.c_str(),
            static_cast<long long>(entry.size));
        total += entry.size;
    }

    // --- the totals ---
    //
    // `bytes` is what the entries hold, `file` is what the image weighs. The gap
    // between them is the zip's own bookkeeping: one local header per entry, the
    // central directory, and the record that ends it.
    rv_pdklib::rv_fprintf(stdout, "[total]\n\n");
    rv_pdklib::rv_fprintf(stdout, "entries\t%zu\n", archive.entries.size());
    rv_pdklib::rv_fprintf(stdout, "bytes\t%lld\n", static_cast<long long>(total));
    rv_pdklib::rv_fprintf(stdout, "file\t%lld\n", static_cast<long long>(archive.bytes.size()));

    return 0;
}
