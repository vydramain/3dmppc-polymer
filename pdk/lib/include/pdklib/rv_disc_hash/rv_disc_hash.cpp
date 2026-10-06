#include "rv_disc_hash.hpp"

#include <cstdint>
#include <cstring>
#include <elf.h>

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"

namespace rv_pdklib
{

namespace
{

// --- SHA-256 (FIPS 180-4), self-contained: no crypto dependency for one hash. ---
constexpr uint32_t sha256_k[64] = {
    0x428a2f98,
    0x71374491,
    0xb5c0fbcf,
    0xe9b5dba5,
    0x3956c25b,
    0x59f111f1,
    0x923f82a4,
    0xab1c5ed5,
    0xd807aa98,
    0x12835b01,
    0x243185be,
    0x550c7dc3,
    0x72be5d74,
    0x80deb1fe,
    0x9bdc06a7,
    0xc19bf174,
    0xe49b69c1,
    0xefbe4786,
    0x0fc19dc6,
    0x240ca1cc,
    0x2de92c6f,
    0x4a7484aa,
    0x5cb0a9dc,
    0x76f988da,
    0x983e5152,
    0xa831c66d,
    0xb00327c8,
    0xbf597fc7,
    0xc6e00bf3,
    0xd5a79147,
    0x06ca6351,
    0x14292967,
    0x27b70a85,
    0x2e1b2138,
    0x4d2c6dfc,
    0x53380d13,
    0x650a7354,
    0x766a0abb,
    0x81c2c92e,
    0x92722c85,
    0xa2bfe8a1,
    0xa81a664b,
    0xc24b8b70,
    0xc76c51a3,
    0xd192e819,
    0xd6990624,
    0xf40e3585,
    0x106aa070,
    0x19a4c116,
    0x1e376c08,
    0x2748774c,
    0x34b0bcb5,
    0x391c0cb3,
    0x4ed8aa4a,
    0x5b9cca4f,
    0x682e6ff3,
    0x748f82ee,
    0x78a5636f,
    0x84c87814,
    0x8cc70208,
    0x90befffa,
    0xa4506ceb,
    0xbef9a3f7,
    0xc67178f2,
};

// Rotation and size constants: FIPS 180-4 reference sections noted below.
constexpr int word_bits = 32;
constexpr int big_sigma0_rot_a = 2;
constexpr int big_sigma0_rot_b = 13;
constexpr int big_sigma0_rot_c = 22;
constexpr int big_sigma1_rot_a = 6;
constexpr int big_sigma1_rot_b = 11;
constexpr int big_sigma1_rot_c = 25;
constexpr int sigma0_rot_a = 7;
constexpr int sigma0_rot_b = 18;
constexpr int sigma0_rot_c = 3;
constexpr int sigma1_rot_a = 17;
constexpr int sigma1_rot_b = 19;
constexpr int sigma1_rot_c = 10;
constexpr int block_size = 64;
constexpr int message_schedule_initial = 16;
constexpr int message_schedule_full = 64;
constexpr int w_schedule_lag_2 = 2;
constexpr int w_schedule_lag_7 = 7;
constexpr int w_schedule_lag_15 = 15;
constexpr int w_schedule_lag_16 = 16;
constexpr int big_endian_shift_1 = 24;
constexpr int big_endian_shift_2 = 16;
constexpr int big_endian_shift_3 = 8;
constexpr int hash_state_words = 8;
constexpr int bytes_per_word = 4;
constexpr int hash_length_bytes = 8;
constexpr int bits_per_byte = 8;
constexpr int hash_digest_bytes = 32;
constexpr int hash_padding_boundary = 56;

inline uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (word_bits - n));
}

// This is the initial state of SHA-256.
// The algorithm starts its computation from these eight specific numbers - they are
// fixed by the standard.
// If they are replaced with zeros or other values,
// the result will no longer be a standard SHA-256.
//
// - h[8]             - initial values that will change as data is processed.
//                      The final values become the hash.
// - buf_len = 0      - no data in the buffer yet.
// - total_len = 0    - no byte has arrived yet.
// - buf[64]          - buffer for the next block.
//                      Not initialised: the needed bytes are filled in before processing.
struct sha256_ctx {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char buf[64];
    size_t buf_len = 0;
    uint64_t total_len = 0;
};
void sha256_block(sha256_ctx &ctx, const unsigned char *p)
{
    uint32_t w[message_schedule_full];
    // Turns 64 bytes into 16 uint32_t numbers.
    for (int i = 0; i < message_schedule_initial; ++i) {
        w[i] = (uint32_t(p[i * bytes_per_word]) << big_endian_shift_1) |
            (uint32_t(p[i * bytes_per_word + 1]) << big_endian_shift_2) |
            (uint32_t(p[i * bytes_per_word + 2]) << big_endian_shift_3) | uint32_t(p[i * bytes_per_word + 3]);
    }

    // The second loop fills w[16] ... w[63] from the previous elements,
    // using rotations, XOR (^) and addition:
    // The result is 64 words - one for each round.
    // All of them depend on the original block.
    for (int i = message_schedule_initial; i < message_schedule_full; ++i) {
        uint32_t s0 = rotr(w[i - w_schedule_lag_15], sigma0_rot_a) ^ rotr(w[i - w_schedule_lag_15], sigma0_rot_b) ^
            (w[i - w_schedule_lag_15] >> sigma0_rot_c);
        uint32_t s1 = rotr(w[i - w_schedule_lag_2], sigma1_rot_a) ^ rotr(w[i - w_schedule_lag_2], sigma1_rot_b) ^
            (w[i - w_schedule_lag_2] >> sigma1_rot_c);
        w[i] = w[i - w_schedule_lag_16] + s0 + w[i - w_schedule_lag_7] + s1;
    }

    uint32_t a = ctx.h[0];
    uint32_t b = ctx.h[1];
    uint32_t c = ctx.h[2];
    uint32_t d = ctx.h[3];
    uint32_t e = ctx.h[4];
    uint32_t f = ctx.h[5];
    uint32_t g = ctx.h[6];
    uint32_t h = ctx.h[7];

    // Performs 64 rounds of mixing
    for (int i = 0; i < message_schedule_full; ++i) {
        uint32_t s1 = rotr(e, big_sigma1_rot_a) ^ rotr(e, big_sigma1_rot_b) ^ rotr(e, big_sigma1_rot_c);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = rotr(a, big_sigma0_rot_a) ^ rotr(a, big_sigma0_rot_b) ^ rotr(a, big_sigma0_rot_c);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.h[0] += a;
    ctx.h[1] += b;
    ctx.h[2] += c;
    ctx.h[3] += d;
    ctx.h[4] += e;
    ctx.h[5] += f;
    ctx.h[6] += g;
    ctx.h[7] += h;
}

// Adds len bytes to the SHA-256 computation: accumulates data in the buffer
// and processes every full 64-byte block.
// An incomplete block stays in the buffer until the next update or finalisation.
void sha256_update(sha256_ctx &ctx, const unsigned char *data, size_t len)
{
    ctx.total_len += len;
    while (len > 0) {
        size_t take = block_size - ctx.buf_len;
        if (take > len) {
            take = len;
        }
        std::memcpy(ctx.buf + ctx.buf_len, data, take);
        ctx.buf_len += take;
        data += take;
        len -= take;
        if (ctx.buf_len == block_size) {
            sha256_block(ctx, ctx.buf);
            ctx.buf_len = 0;
        }
    }
}

// Finalises the SHA-256 computation: pads the remaining data, processes
// the closing blocks and writes the full 32-byte hash into digest.
void sha256_final(sha256_ctx &ctx, unsigned char digest[hash_digest_bytes])
{
    // Length of the original data in bits, before the padding added below.
    uint64_t bit_len = ctx.total_len * bits_per_byte;
    {
        // The buffer has between 0 and (block_size - 1) not-yet-processed bytes left.
        size_t buf_len = ctx.buf_len;

        // 0x80 = 10000000: the mandatory 1 bit followed by the first seven zero bits
        // of the padding. Written directly so as not to grow total_len via update.
        ctx.buf[buf_len++] = 0x80;

        // The last 8 bytes of the block are reserved for the length of the original data.
        // If there is no room for it, finish the current block with zeros and process it.
        if (buf_len > hash_padding_boundary) {
            std::memset(ctx.buf + buf_len, 0, block_size - buf_len);
            sha256_block(ctx, ctx.buf);

            // The next block is assembled from the start of the same buffer.
            buf_len = 0;
        }

        // Fill the free bytes up to the length field with zeros (indices up to padding_boundary - 1 inclusive).
        std::memset(ctx.buf + buf_len, 0, hash_padding_boundary - buf_len);

        // Write the length in bits into bytes (padding_boundary) to (block_size - 1), big-endian.
        for (int i = 0; i < hash_length_bytes; ++i) {
            const int shift_amount = hash_length_bytes * bits_per_byte - bits_per_byte * (i + 1);
            ctx.buf[hash_padding_boundary + i] = static_cast<unsigned char>(bit_len >> shift_amount);
        }

        // After processing the last block, ctx.h holds the final hash state.
        sha256_block(ctx, ctx.buf);
    }

    // Write the eight 32-bit state words into 32 bytes of digest:
    // each word big-endian (most significant byte first).
    for (int i = 0; i < hash_state_words; ++i) {
        digest[i * bytes_per_word] = static_cast<unsigned char>(ctx.h[i] >> big_endian_shift_1);
        digest[i * bytes_per_word + 1] = static_cast<unsigned char>(ctx.h[i] >> big_endian_shift_2);
        digest[i * bytes_per_word + 2] = static_cast<unsigned char>(ctx.h[i] >> big_endian_shift_3);
        digest[i * bytes_per_word + 3] = static_cast<unsigned char>(ctx.h[i]);
    }
}

// --- ELF64 section lookup, bounds-checked against an untrusted buffer. ---
bool in_bounds(std::size_t elf_size, uint64_t off, uint64_t len)
{
    if (off > elf_size) {
        return false;
    }
    // off + len must not overflow and must stay within elf_size.
    return len <= elf_size - off;
}

// Finds a section by name. Returns RV_OK and fills `sh` on success (including
// "not found", which reports offset=0/size=0 via `found=false`).
int find_section(const unsigned char *elf,
    std::size_t elf_size,
    const char *name,
    bool &found,
    uint64_t &sh_offset,
    uint64_t &sh_size,
    uint32_t &sh_type,
    std::string &error)
{
    found = false;

    if (elf_size < sizeof(Elf64_Ehdr)) {
        error = "ELF image is smaller than an ELF64 header.";
        return RV_ERR_INVAL;
    }

    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, elf, sizeof(ehdr));

    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        error = "Missing ELF magic.";
        return RV_ERR_INVAL;
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        error = "Only ELFCLASS64 is supported.";
        return RV_ERR_INVAL;
    }
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        error = "Only little-endian ELF is supported.";
        return RV_ERR_INVAL;
    }

    if (ehdr.e_shentsize != sizeof(Elf64_Shdr)) {
        error = "Unexpected e_shentsize.";
        return RV_ERR_INVAL;
    }
    if (ehdr.e_shnum == 0) {
        error = "ELF image has no section headers.";
        return RV_ERR_INVAL;
    }
    if (!in_bounds(elf_size, ehdr.e_shoff, uint64_t(ehdr.e_shnum) * sizeof(Elf64_Shdr))) {
        error = "Section header table is out of bounds.";
        return RV_ERR_INVAL;
    }
    if (ehdr.e_shstrndx == SHN_UNDEF || ehdr.e_shstrndx >= ehdr.e_shnum) {
        error = "Invalid section header string table index.";
        return RV_ERR_INVAL;
    }

    const unsigned char *shtab = elf + ehdr.e_shoff;

    Elf64_Shdr strtab_shdr;
    std::memcpy(&strtab_shdr, shtab + uint64_t(ehdr.e_shstrndx) * sizeof(Elf64_Shdr), sizeof(strtab_shdr));
    if (!in_bounds(elf_size, strtab_shdr.sh_offset, strtab_shdr.sh_size)) {
        error = "Section header string table is out of bounds.";
        return RV_ERR_INVAL;
    }
    const char *strtab = reinterpret_cast<const char *>(elf + strtab_shdr.sh_offset);
    uint64_t strtab_size = strtab_shdr.sh_size;

    for (uint16_t i = 0; i < ehdr.e_shnum; ++i) {
        Elf64_Shdr shdr;
        std::memcpy(&shdr, shtab + uint64_t(i) * sizeof(Elf64_Shdr), sizeof(shdr));

        if (shdr.sh_name >= strtab_size) {
            error = "Section name offset is out of bounds.";
            return RV_ERR_INVAL;
        }
        // Find the NUL terminator within the string table, do not run past it.
        uint64_t max_len = strtab_size - shdr.sh_name;
        const char *candidate = strtab + shdr.sh_name;
        std::size_t candidate_len = 0;
        while (candidate_len < max_len && candidate[candidate_len] != '\0') {
            ++candidate_len;
        }
        if (candidate_len == max_len) {
            error = "Section name is not NUL-terminated within the string table.";
            return RV_ERR_INVAL;
        }

        if (std::strcmp(candidate, name) != 0) {
            continue;
        }

        if (shdr.sh_type != SHT_NOBITS) {
            if (!in_bounds(elf_size, shdr.sh_offset, shdr.sh_size)) {
                error = std::string("Section '") + name + "' data is out of bounds.";
                return RV_ERR_INVAL;
            }
        }

        found = true;
        sh_offset = shdr.sh_offset;
        sh_size = shdr.sh_size;
        sh_type = shdr.sh_type;
        return RV_OK;
    }

    return RV_OK; // not found is not an error: contributes size 0.
}

int feed_section(const unsigned char *elf, std::size_t elf_size, const char *name, sha256_ctx &ctx, std::string &error)
{
    bool found = false;
    uint64_t sh_offset = 0;
    uint64_t sh_size = 0;
    uint32_t sh_type = SHT_NULL;

    if (find_section(elf, elf_size, name, found, sh_offset, sh_size, sh_type, error) != RV_OK) {
        return RV_ERR_INVAL;
    }

    uint64_t size = found ? sh_size : 0;
    unsigned char size_le[hash_length_bytes];
    for (int i = 0; i < hash_length_bytes; ++i) {
        size_le[i] = static_cast<unsigned char>(size >> (bits_per_byte * i));
    }
    sha256_update(ctx, size_le, sizeof(size_le));

    if (found && sh_type != SHT_NOBITS && sh_size > 0) {
        sha256_update(ctx, elf + sh_offset, sh_size);
    }

    return RV_OK;
}

} // namespace

int rv_disc_hash_magic_offset(const unsigned char *elf, std::size_t elf_size, std::size_t &magic_offset, std::string &error)
{
    bool found = false;
    uint64_t sh_offset = 0;
    uint64_t sh_size = 0;
    uint32_t sh_type = SHT_NULL;

    if (find_section(elf, elf_size, RV_MPPC_SECTION_NAME_DEF, found, sh_offset, sh_size, sh_type, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (!found) {
        error = "ELF image has no " RV_MPPC_SECTION_NAME_DEF " section.";
        return RV_ERR_INVAL;
    }
    if (sh_type == SHT_NOBITS) {
        error = "Version note section has no file contents.";
        return RV_ERR_INVAL;
    }

    if (sh_size < sizeof(Elf64_Nhdr)) {
        error = "Version note section is smaller than an ELF note header.";
        return RV_ERR_INVAL;
    }

    Elf64_Nhdr nhdr;
    std::memcpy(&nhdr, elf + sh_offset, sizeof(nhdr));

    constexpr uint64_t RV_DISC_HASH_OWNER_SIZE = sizeof(RV_MPPC_NOTE_OWNER_DEF);
    constexpr uint64_t RV_DISC_HASH_DESC_SIZE = sizeof(rv_mppc_note_desc);

    if (nhdr.n_namesz != RV_DISC_HASH_OWNER_SIZE) {
        error = "Version note owner size does not match RV_MPPC_NOTE_OWNER_DEF.";
        return RV_ERR_INVAL;
    }
    if (nhdr.n_descsz != RV_DISC_HASH_DESC_SIZE) {
        error = "Version note descriptor size does not match rv_mppc_note_desc.";
        return RV_ERR_INVAL;
    }
    if (nhdr.n_type != RV_MPPC_NOTE_TYPE) {
        error = "Version note type does not match RV_MPPC_NOTE_TYPE.";
        return RV_ERR_INVAL;
    }

    auto align4 = [](uint64_t n) -> uint64_t {
        return (n + 3) & ~uint64_t(3);
    };

    const uint64_t owner_offset = sh_offset + sizeof(Elf64_Nhdr);
    const uint64_t aligned_owner_size = align4(nhdr.n_namesz);
    const uint64_t desc_offset = owner_offset + aligned_owner_size;
    const uint64_t aligned_desc_size = align4(nhdr.n_descsz);
    const uint64_t note_end = desc_offset + aligned_desc_size;

    if (!in_bounds(elf_size, owner_offset, aligned_owner_size) || !in_bounds(elf_size, desc_offset, aligned_desc_size) ||
        note_end > sh_offset + sh_size) {
        error = "Version note extends outside its section.";
        return RV_ERR_INVAL;
    }

    if (std::memcmp(elf + owner_offset, RV_MPPC_NOTE_OWNER_DEF, RV_DISC_HASH_OWNER_SIZE) != 0) {
        error = "Version note owner does not match RV_MPPC_NOTE_OWNER_DEF.";
        return RV_ERR_INVAL;
    }

    // magic is at descriptor offset 0 (rv_mppc_note_desc's first field).
    magic_offset = static_cast<std::size_t>(desc_offset);
    return RV_OK;
}

int rv_disc_hash_compute(const unsigned char *elf,
    std::size_t elf_size,
    unsigned char out[RV_DISC_HASH_BYTES],
    std::string &error)
{
    sha256_ctx ctx;

    static const char *const sections[] = { ".text", ".rodata", ".data" };
    for (const char *name : sections) {
        if (feed_section(elf, elf_size, name, ctx, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }

    unsigned char digest[hash_digest_bytes];
    sha256_final(ctx, digest);
    std::memcpy(out, digest, RV_DISC_HASH_BYTES);
    return RV_OK;
}

} // namespace rv_pdklib
