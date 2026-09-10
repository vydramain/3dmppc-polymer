#include "rv_pboot_check.hpp"

#include <cstdint>
#include <limits>

#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cm/rv_pccard.hpp"
#include "rv_pconsole/rv_pchost_sdl3.hpp"

namespace rv_3dmppc
{

namespace
{

// head_ + tail_ per bucket in rv_pcotable (rv_pcotable.hpp): two int32_t.
constexpr int64_t RV_PCOTABLE_BYTES_PER_BUCKET = 8;

// rv_pcfbuf (rv_pcfbuf.hpp): color_ (uint16_t) + depth_ (int32_t) +
// argb_ (uint32_t) per pixel.
constexpr int64_t RV_PCFBUF_BYTES_PER_PIXEL = 2 + 4 + 4;

// rv_pccard's on-disk header (rv_pccard.cpp): magic+version+slot count+size,
// plus one 8-byte length entry per slot, ahead of the slot payloads.
constexpr int64_t RV_PCCARD_HEADER_BYTES = 32;
constexpr int64_t RV_PCCARD_LENGTH_ENTRY_BYTES = 8;

// A voice mask is an int64_t carrying bits 0..62 (pdk/ca/rv_ca.h), so 63 is
// the most voices any console can ever name — see RV_PCCA_MAX_VOICES in
// rv_pcca.cpp, whose clamp to this same limit is the unreachable backstop.
constexpr int64_t RV_PCCA_MAX_VOICES = 63;

// Non-negative a*b into `out`, refusing by name on overflow.
bool mul_overflow(const char *field, int64_t a, int64_t b, int64_t &out)
{
    if (a != 0 && b > std::numeric_limits<int64_t>::max() / a) {
        RV_LOG_ERR("pccheck", "'{}' overflows: {} * {}", field, a, b);
        return true;
    }
    out = a * b;
    return false;
}

// total += amount, refusing by name on overflow.
bool add_overflow(const char *field, int64_t &total, int64_t amount)
{
    if (amount > std::numeric_limits<int64_t>::max() - total) {
        RV_LOG_ERR("pccheck", "'{}' overflows the memory total", field);
        return true;
    }
    total += amount;
    return false;
}

// A field must be strictly positive when its subsystem is `active`. A
// negative value is malformed regardless — an unset (zero) field of a
// switched-off subsystem is the only value this passes without `active`.
bool bad_field(const char *field, int64_t value, bool active)
{
    if (value < 0) {
        RV_LOG_ERR("pccheck", "'{}' is negative ({})", field, value);
        return true;
    }
    if (active && value == 0) {
        RV_LOG_ERR("pccheck", "'{}' must be positive, this subsystem is on", field);
        return true;
    }
    return false;
}

} // namespace

// The budget is stated in console units (a disc must look the same on every
// backend), while the machine below is measured in host bytes. The total
// therefore counts both the disc's own pools (video memory, sound memory,
// script heap) and the buffers the console itself needs to realise that
// budget (framebuffer, ordering table, primitive buffer, card image, port
// slots).
int64_t rv_pboot_check_budget(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pboot_mode_info &machine)
{
    const bool audio_on = machine.audio_enabled;

    // Sanity of the declared numbers. The rasterizer's own memory (cv.*) is
    // required whether cv=null or not (nothing here looks at display bounds), so
    // those fields are always active. pccio has no on/off switch.
    if (bad_field("budget.pcca.voice_count", budget.pcca.voice_count, audio_on) ||
        bad_field("budget.pcca.sound_memory_size", budget.pcca.sound_memory_size, audio_on) ||
        bad_field("budget.pccv.screen_width", budget.pccv.screen_width, true) ||
        bad_field("budget.pccv.screen_height", budget.pccv.screen_height, true) ||
        bad_field("budget.pccv.texture_max_width", budget.pccv.texture_max_width, true) ||
        bad_field("budget.pccv.texture_max_height", budget.pccv.texture_max_height, true) ||
        bad_field("budget.pccv.video_memory_size", budget.pccv.video_memory_size, true) ||
        bad_field("budget.pccv.frame_capacity", budget.pccv.frame_capacity, true) ||
        bad_field("budget.pccv.ot_bucket_count", budget.pccv.ot_bucket_count, true) ||
        bad_field("budget.pccio.iport_count", budget.pccio.iport_count, true) ||
        bad_field("budget.pccm.card_slots", budget.pccm.card_slots, true) ||
        bad_field("budget.pccm.card_slot_size", budget.pccm.card_slot_size, true) ||
        bad_field("budget.pccl.script_memory_size", budget.pccl.script_memory_size, false)) {
        return RV_ERR_INVAL;
    }

    // The lua machine is the one subsystem a disc may legally not have at all,
    // so it is checked with `active` false: zero passes, a negative does not.
    // A disc that declared no [budget.pccl] runs with no VM, exactly as every
    // disc did before scripting existed.
    const bool scripting = budget.pccl.script_memory_size > 0;

    // voice_count is never silently reduced: either the mask can name every
    // requested voice, or the run is refused by name here — rv_pcca.cpp's own
    // clamp to RV_PCCA_MAX_VOICES must never actually fire.
    if (audio_on && budget.pcca.voice_count > RV_PCCA_MAX_VOICES) {
        RV_LOG_ERR("pccheck",
            "'budget.pcca.voice_count' asks for {}, over the {} this console can name "
            "(a voice mask carries bits 0..62)",
            budget.pcca.voice_count, RV_PCCA_MAX_VOICES);
        return RV_ERR_INVAL;
    }

    int64_t total = 0;

    // Video RAM pool (rv_pccv::rv_pccv -> rv_pcvram(video_memory_size)):
    // the pool is exactly video_memory_size bytes, no product involved.
    if (add_overflow("budget.pccv.video_memory_size", total, budget.pccv.video_memory_size)) {
        return RV_ERR_INVAL;
    }

    // Framebuffer (rv_pcfbuf): width * height * (color + depth + argb).
    int64_t pixels = 0;
    int64_t fbuf_bytes = 0;
    if (mul_overflow("budget.pccv.screen_width * screen_height", budget.pccv.screen_width,
            budget.pccv.screen_height, pixels) ||
        mul_overflow("budget.pccv.screen_width * screen_height * bytes_per_pixel", pixels,
            RV_PCFBUF_BYTES_PER_PIXEL, fbuf_bytes) ||
        add_overflow("budget.pccv.screen_width * screen_height * bytes_per_pixel", total,
            fbuf_bytes)) {
        return RV_ERR_INVAL;
    }

    // Ordering table (rv_pcotable): ot_bucket_count * (head_ + tail_).
    int64_t otable_bytes = 0;
    if (mul_overflow("budget.pccv.ot_bucket_count", budget.pccv.ot_bucket_count,
            RV_PCOTABLE_BYTES_PER_BUCKET, otable_bytes) ||
        add_overflow("budget.pccv.ot_bucket_count", total, otable_bytes)) {
        return RV_ERR_INVAL;
    }

    // Primitive buffer (rv_pccv::rv_pccv -> primitives_.reserve(frame_capacity)):
    // frame_capacity * sizeof(rv_primitive).
    int64_t primitives_bytes = 0;
    if (mul_overflow("budget.pccv.frame_capacity", budget.pccv.frame_capacity,
            static_cast<int64_t>(sizeof(rv_primitive)), primitives_bytes) ||
        add_overflow("budget.pccv.frame_capacity", total, primitives_bytes)) {
        return RV_ERR_INVAL;
    }

    // Memory-card image (rv_pccard): header + one length entry per slot, plus
    // the slot payloads themselves.
    int64_t card_payload_bytes = 0;
    int64_t card_table_bytes = 0;
    if (mul_overflow("budget.pccm.card_slots * card_slot_size", budget.pccm.card_slots,
            budget.pccm.card_slot_size, card_payload_bytes) ||
        mul_overflow("budget.pccm.card_slots", budget.pccm.card_slots,
            RV_PCCARD_LENGTH_ENTRY_BYTES, card_table_bytes) ||
        add_overflow("budget.pccm.card_slots * card_slot_size", total, card_payload_bytes) ||
        add_overflow("budget.pccm.card_slots", total, card_table_bytes) ||
        add_overflow("budget.pccm.card_slots", total, RV_PCCARD_HEADER_BYTES)) {
        return RV_ERR_INVAL;
    }

    // rv_pccard refuses at construction to hold an image above its own
    // ceiling (rv_pccard.hpp), logs it, and lets the disc run anyway with no
    // card. Refuse it here by name instead, before any disc code loads, so a
    // card the console cannot actually hold never gets that far.
    const int64_t card_image_bytes =
        RV_PCCARD_HEADER_BYTES + card_table_bytes + card_payload_bytes;
    if (card_image_bytes > rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES) {
        RV_LOG_ERR("pccheck",
            "'budget.pccm.card_slots' ({}) * 'budget.pccm.card_slot_size' ({}) needs a {} "
            "byte(s) card image, over the {} byte(s) this console's memory card can hold",
            budget.pccm.card_slots, budget.pccm.card_slot_size, card_image_bytes,
            rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES);
        return RV_ERR_INVAL;
    }

    // Port slots (rv_pchost_sdl3::configure -> ports_.assign(iport_count, ...)):
    // iport_count * sizeof(rv_pcport). Uncosted, this is how an absurd
    // iport_count reaches configure()'s std::vector::assign() and aborts the
    // process with an unhandled std::length_error instead of being refused
    // here by name.
    int64_t iports_bytes = 0;
    if (mul_overflow("budget.pccio.iport_count", budget.pccio.iport_count,
            rv_pchost_sdl3::port_bytes(), iports_bytes) ||
        add_overflow("budget.pccio.iport_count", total, iports_bytes)) {
        return RV_ERR_INVAL;
    }

    // Sound RAM (rv_pcca::rv_pcca -> sram_.emplace(sound_memory_size, ...)),
    // only when audio is actually switched on: a run with audio off neither
    // checks nor counts the disc's sound memory.
    if (audio_on) {
        if (add_overflow("budget.pcca.sound_memory_size", total, budget.pcca.sound_memory_size)) {
            return RV_ERR_INVAL;
        }
    }

    // Script RAM (rv_pccl -> lua_newstate with a budgeted allocator), only when
    // the disc declared a lua machine. Uncosted, a disc could ask for half a
    // gigabyte of lua heap and still pass this check, then fail at the first
    // allocation inside the VM instead of being refused here by name.
    if (scripting) {
        if (add_overflow("budget.pccl.script_memory_size", total, budget.pccl.script_memory_size)) {
            return RV_ERR_INVAL;
        }
    }

    // Compare against what the machine actually has.
    if (machine.ram_available < 0) {
        RV_LOG_ERR("pccheck",
            "machine RAM unknown, cannot show disc's {} byte(s) fit", total);
        return RV_ERR_INVAL;
    }

    if (total > machine.ram_available) {
        RV_LOG_ERR("pccheck", "disc needs {} byte(s) of RAM, this machine has {}", total,
            machine.ram_available);
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

} // namespace rv_3dmppc
