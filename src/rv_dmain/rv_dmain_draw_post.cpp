// rv_dmain: status POST display.
#include "rv_dmain.hpp"

#include <cstdio>
#include <span>

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_font/rv_font.hpp"

#include "rv_dmain_draw_shared.hpp"

namespace rv_service
{

namespace
{

constexpr int64_t RV_DMAIN_POST_BYTES_PER_MB = 1024 * 1024;
constexpr int64_t RV_DMAIN_POST_BYTES_PER_KB = 1024;

void format_bytes(char *out, std::size_t cap, int64_t bytes)
{
    // The buffer must fit the widest int64 plus a unit; snprintf truncating a
    // diagnostic silently is the last thing a diagnostic screen should do.
    if (bytes >= RV_DMAIN_POST_BYTES_PER_MB && bytes % RV_DMAIN_POST_BYTES_PER_MB == 0) {
        std::snprintf(out, cap, "%lld MB", static_cast<long long>(bytes / RV_DMAIN_POST_BYTES_PER_MB));
    } else if (bytes >= RV_DMAIN_POST_BYTES_PER_KB) {
        std::snprintf(out, cap, "%lld KB", static_cast<long long>(bytes / RV_DMAIN_POST_BYTES_PER_KB));
    } else {
        std::snprintf(out, cap, "%lld B", static_cast<long long>(bytes));
    }
}

// POST header. Two lines that say what this machine IS - the virtual
// budget it imposes on itself - and bottom rows saying whether each
// subsystem answered. The numbers come straight from the contract's geometry
// queries, so a line that prints at all is a query that worked.
//
// An earlier version of this screen captioned the picture instead ("textures",
// "cube", "ticks", "bar", "south") and was useless: those are the names of THIS
// file's internals, and nobody outside it can know that "south" is what the
// input contract calls the bottom face button.

// Constants for the POST section
constexpr int32_t RV_DMAIN_DEPTH_PANEL = 880; // the slab text sits on
constexpr int32_t RV_DMAIN_DEPTH_TEXT = 900;  // over everything, it explains it
constexpr const char *RV_DMAIN_TITLE = "3DMPPC";
constexpr const char *RV_DMAIN_NO_DISC = "NO DISC INSERTED";
// Named by the KEY a person can press. "south" is what the input contract calls
// the bottom face button, and nobody outside that header knows it.
constexpr const char *RV_DMAIN_HINTS = "Z sound   ESC power off";

// Two probe rows over the pads row, 10 px apart.
constexpr float RV_DMAIN_POST_BAR_HEIGHT = 34.0f;
// The bottom slab turns this colour when the font is gone: with no font the
// slab is the only sign left, and the reason is the WARN of build_font().
constexpr rv_color RV_DMAIN_POST_SLAB_FAILED{ 120, 24, 28 };
constexpr int RV_DMAIN_POST_MARGIN_H = 4;
constexpr int RV_DMAIN_POST_MARGIN_TOP = 2;
constexpr int RV_DMAIN_POST_BUDGET_LINE_Y = 13;
// TEX and FONT: the row above the device probes.
constexpr int RV_DMAIN_POST_UPPER_PROBE_ROW_OFFSET_FROM_BOTTOM = 31;
constexpr int RV_DMAIN_POST_PROBE_ROW_OFFSET_FROM_BOTTOM = 21;
constexpr int RV_DMAIN_POST_PADS_ROW_OFFSET_FROM_BOTTOM = 11;
constexpr int RV_DMAIN_POST_PROBE_LABEL_SPACING = 8;
constexpr int RV_DMAIN_POST_PROBE_VERDICT_SPACING = 10;
constexpr std::size_t RV_DMAIN_POST_BUDGET_BUFFER_SIZE = 96;
constexpr std::size_t RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE = 24;
constexpr std::size_t RV_DMAIN_POST_PADS_BUFFER_SIZE = 32;
constexpr std::size_t RV_DMAIN_POST_PROBE_COUNT = 4;

} // namespace

void rv_dmain::draw_post_row(int, const char *, const char *, const char *, bool)
{
}

void rv_dmain::draw_post()
{
    rv_cv *cv = rv_pdko_cv(pdk_);
    auto file = [cv](const rv_primitive &primitive) {
        rv_cv_frame_put(cv, &primitive);
    };

    const int width = static_cast<int>(screen_width_);
    const int height = static_cast<int>(screen_height_);
    const rv_color slab{ 12, 14, 22 };

    const bool font_ok = addr_font_ != 0;
    const rv_color bottom_colour = font_ok ? slab : RV_DMAIN_POST_SLAB_FAILED;
    const rv_primitive bottom_slab = rv_dmain_detail::make_bar(0.0f,
        static_cast<float>(height) - RV_DMAIN_POST_BAR_HEIGHT,
        static_cast<float>(width),
        RV_DMAIN_POST_BAR_HEIGHT,
        bottom_colour,
        RV_DMAIN_DEPTH_PANEL);
    rv_cv_frame_put(cv, &bottom_slab);

    // No font, no text: the red slab alone is the sign.
    if (!font_ok) {
        return;
    }

    const rv_pdklib::rv_font_style ink = rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_, RV_DMAIN_DEPTH_TEXT, 1);
    const rv_pdklib::rv_font_style bad =
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_bad_, RV_DMAIN_DEPTH_TEXT, 1);

    // Opaque slabs, not a dim overlay: the console has no blending, and light
    // ink over lit geometry is exactly what made the first version unreadable.
    const rv_primitive top_slab =
        rv_dmain_detail::make_bar(0.0f, 0.0f, static_cast<float>(width), RV_DMAIN_POST_BAR_HEIGHT, slab, RV_DMAIN_DEPTH_PANEL);
    rv_cv_frame_put(cv, &top_slab);
    rv_pdklib::rv_font_draw(ink, RV_DMAIN_POST_MARGIN_H, RV_DMAIN_POST_MARGIN_TOP, RV_DMAIN_TITLE, file);
    rv_pdklib::rv_font_draw(ink,
        width - RV_DMAIN_POST_MARGIN_H - rv_pdklib::rv_font_measure_width(RV_DMAIN_NO_DISC, 1),
        RV_DMAIN_POST_MARGIN_TOP,
        RV_DMAIN_NO_DISC,
        file);

    // The budget line. Saying VIRTUAL out loud matters: none of this is what the
    // host has, all of it is what the fantasy machine is defined to have, and
    // the pools really do refuse past the line.
    char budget[RV_DMAIN_POST_BUDGET_BUFFER_SIZE];
    char vram[RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE];
    char sram[RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE];
    format_bytes(vram, sizeof(vram), video_memory_size_);
    format_bytes(sram, sizeof(sram), sound_memory_size_);
    std::snprintf(budget,
        sizeof(budget),
        "VIRTUAL %lldx%lld %s %lldv %s",
        static_cast<long long>(screen_width_),
        static_cast<long long>(screen_height_),
        vram,
        static_cast<long long>(voice_count_),
        sram);
    rv_pdklib::rv_font_draw(ink, RV_DMAIN_POST_MARGIN_H, RV_DMAIN_POST_BUDGET_LINE_Y, budget, file);

    struct rv_dmain_probe {
        const char *label;
        bool ok;
        bool absent; // legal "nothing there", which is not a failure
    };
    const rv_dmain_probe upper[] = {
        { "TEX", texture_result_ == RV_OK && idx4_result_ == RV_OK, false },
        { "FONT", font_result_ == RV_OK, false },
    };
    const rv_dmain_probe lower[RV_DMAIN_POST_PROBE_COUNT] = {
        { "AUDIO", beep_ok_, false },
        { "DRIVE", asset_ok_, asset_bytes_ == 0 },
        { "CARD", card_ok_, false },
        { "PATHS", drive_rejects_paths_, false },
    };

    // Each probe row owns its whole line: four labels and four verdicts already
    // fill 40 columns, and anything sharing the row lands on top of them.
    auto draw_probe_row = [&](std::span<const rv_dmain_probe> probes, int offset_from_bottom) {
        int pen = RV_DMAIN_POST_MARGIN_H;
        for (const rv_dmain_probe &probe : probes) {
            rv_pdklib::rv_font_draw(ink, pen, height - offset_from_bottom, probe.label, file);
            pen += rv_pdklib::rv_font_measure_width(probe.label, 1) + RV_DMAIN_POST_PROBE_LABEL_SPACING;

            const char *mark = probe.absent ? "-" : (probe.ok ? "OK" : "FAIL");
            rv_pdklib::rv_font_draw(probe.ok || probe.absent ? ink : bad, pen, height - offset_from_bottom, mark, file);
            pen += rv_pdklib::rv_font_measure_width(mark, 1) + RV_DMAIN_POST_PROBE_VERDICT_SPACING;
        }
    };
    draw_probe_row(upper, RV_DMAIN_POST_UPPER_PROBE_ROW_OFFSET_FROM_BOTTOM);
    draw_probe_row(lower, RV_DMAIN_POST_PROBE_ROW_OFFSET_FROM_BOTTOM);

    char pads[RV_DMAIN_POST_PADS_BUFFER_SIZE];
    std::snprintf(pads,
        sizeof(pads),
        "%lld pad(s) boot %lu",
        static_cast<long long>(pads_connected_),
        static_cast<unsigned long>(boot_count_));
    rv_pdklib::rv_font_draw(ink, RV_DMAIN_POST_MARGIN_H, height - RV_DMAIN_POST_PADS_ROW_OFFSET_FROM_BOTTOM, pads, file);
    rv_pdklib::rv_font_draw(ink,
        width - RV_DMAIN_POST_MARGIN_H - rv_pdklib::rv_font_measure_width(RV_DMAIN_HINTS, 1),
        height - RV_DMAIN_POST_PADS_ROW_OFFSET_FROM_BOTTOM,
        RV_DMAIN_HINTS,
        file);
}

} // namespace rv_service
