#pragma once

#include <cstddef>
#include <cstdint>

namespace rv_pdktools
{

// --- the .mppcdisc container ---
//
// A plain zip using the STORE method only — no compression, ever. A stored entry
// is read with one seek and one read straight out of the offset the central
// directory gives, so the console needs no zlib: a dependency the machine
// otherwise does not have, in the component that must stay small and auditable.
// The medium is the model too — a CD-ROM held its data as it was.
//
// This header is the ONE place the container's numbers live. The writer
// (rv_burner_zipwrite) and the reader (rv_burner_zipread) both take them from
// here, so a signature or an offset cannot be right on one side and wrong on the
// other. The console's own reader is a separate program and keeps its own copy;
// see src/rv_pconsole/cd/rv_pczip.*.

// --- record signatures ---

inline constexpr uint32_t k_sig_local = 0x04034b50u;   ///< "PK\x03\x04"
inline constexpr uint32_t k_sig_central = 0x02014b50u; ///< "PK\x01\x02"
inline constexpr uint32_t k_sig_eocd = 0x06054b50u;    ///< "PK\x05\x06"

// --- fixed record sizes, signature included, variable-length fields excluded ---

inline constexpr std::size_t k_local_header_size = 30;
inline constexpr std::size_t k_central_header_size = 46;
inline constexpr std::size_t k_eocd_size = 22;

// --- field offsets inside a local file header ---

enum zip_local_offset : std::size_t {
    ZIP_LOCAL_OFF_SIG = 0,
    ZIP_LOCAL_OFF_NAME_LENGTH = 26,
    ZIP_LOCAL_OFF_EXTRA_LENGTH = 28
};

// --- field offsets inside a central directory header ---

enum zip_central_offset : std::size_t {
    ZIP_CENTRAL_OFF_SIG = 0,
    ZIP_CENTRAL_OFF_METHOD = 10,
    ZIP_CENTRAL_OFF_SIZE = 24,
    ZIP_CENTRAL_OFF_NAME_LENGTH = 28,
    ZIP_CENTRAL_OFF_EXTRA_LENGTH = 30,
    ZIP_CENTRAL_OFF_COMMENT_LENGTH = 32,
    ZIP_CENTRAL_OFF_LOCAL_OFFSET = 42
};

// --- field offsets inside the end-of-central-directory record ---

enum zip_eocd_offset : std::size_t {
    ZIP_EOCD_OFF_SIG = 0,
    ZIP_EOCD_OFF_ENTRY_COUNT = 10,
    ZIP_EOCD_OFF_DIRECTORY_SIZE = 12,
    ZIP_EOCD_OFF_DIRECTORY_OFFSET = 16
};

// --- limits ---

/// The only compression method a .mppcdisc may use.
inline constexpr uint16_t k_method_store = 0;

/// Largest comment that can follow the EOCD record, and therefore how far back
/// from the end of a file its signature may sit.
inline constexpr std::size_t k_max_comment_size = 0xFFFFu;

} // namespace rv_pdktools
