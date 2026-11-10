// rv_dmain: the olive window, its title bar, the info panel and the hint buttons.
#include "rv_dmain.hpp"

#include <cstdio>
#include <iterator>
#include <span>
#include <string_view>

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

#include "rv_dmain_ui.hpp"

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

constexpr int RV_DMAIN_POST_BUTTON_GAP = 12; // between the two hint buttons
constexpr int RV_DMAIN_POST_PROBE_ROWS = 2;  // probe lines in the panel, after the budget line
constexpr int RV_DMAIN_POST_PROBES_PER_ROW = 3;
constexpr int RV_DMAIN_POST_PROBE_COUNT = RV_DMAIN_POST_PROBE_ROWS * RV_DMAIN_POST_PROBES_PER_ROW;
constexpr int RV_DMAIN_POST_PIECES_PER_PROBE = 2; // label, verdict
constexpr int RV_DMAIN_POST_PROBE_LINE_FIRST = 1; // panel line of the first probe row
constexpr int RV_DMAIN_POST_PADS_LINE = 3;        // panel line of the pads and boot count
constexpr std::size_t RV_DMAIN_POST_BUDGET_BUFFER_SIZE = 96;
constexpr std::size_t RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE = 24;
constexpr std::size_t RV_DMAIN_POST_PADS_BUFFER_SIZE = 48;
constexpr std::size_t RV_DMAIN_POST_LABEL_BUFFER_SIZE = 24; // gap + label + ": "

static_assert(static_cast<int>(RV_DMAIN_POST_BUDGET_BUFFER_SIZE) - 1 == rv_service::UI_BUDGET_LINE_CHARS &&
        static_cast<int>(RV_DMAIN_POST_PADS_BUFFER_SIZE) - 1 == rv_service::UI_PADS_LINE_CHARS,
    "the frame capacity bound counts these lines: keep UI_*_LINE_CHARS equal to buffer size - 1");

static_assert(std::size(rv_service::UI_PROBE_LABELS) == RV_DMAIN_POST_PROBE_COUNT, "one label per probe");

constexpr std::size_t post_longest_label()
{
    std::size_t longest = 0;
    for (const std::string_view label : rv_service::UI_PROBE_LABELS) {
        longest = label.size() > longest ? label.size() : longest;
    }
    return longest;
}

// A longer text in rv_dmain_ui.hpp must fail the build, not be cut on screen.
static_assert(rv_service::UI_PROBE_GAP.size() + post_longest_label() + rv_service::UI_LABEL_END.size() + 1 <=
        RV_DMAIN_POST_LABEL_BUFFER_SIZE,
    "probe label buffer too small");

// A run of characters in one ink; a panel line is a few of them side by side.
struct rv_post_piece {
    std::string_view text;
    const rv_pdklib::rv_font_style *style;
};

// One line drawn whole or not at all: the full ink width and one line height must lie inside `area`.
// Pieces are monospaced, so each starts at the cell count of the pieces before it.
template <typename Sink>
void draw_line(std::span<const rv_post_piece> pieces, const rv_dmain_detail::rv_ui_rect &area, int x, int y, Sink &&sink)
{
    constexpr int cell = rv_pdklib::rv_font_cell_width;
    int chars = 0;
    for (const rv_post_piece &piece : pieces) {
        chars += static_cast<int>(piece.text.size());
    }
    const int w = chars * cell - (cell - rv_pdklib::rv_font_ink_width);
    const int h = rv_pdklib::rv_font_line_height(1);
    if (x < area.x || y < area.y || x + w > area.x + area.w || y + h > area.y + area.h) {
        return;
    }
    int before = 0;
    for (const rv_post_piece &piece : pieces) {
        rv_pdklib::rv_font_draw(*piece.style, x + before * cell, y, piece.text, sink);
        before += static_cast<int>(piece.text.size());
    }
}

} // namespace

// The olive window around the test tiles: title bar, info panel (budget, probes, pads) and two hint
// buttons. Without the font the panel turns red and no text is drawn: it is then the only sign left,
// and the reason is the WARN of build_font().
void rv_dmain::draw_post()
{
    using namespace rv_dmain_detail;
    namespace theme = rv_pdklib;

    rv_cv *cv = rv_pdko_cv(pdk_);
    const auto file = [cv](const rv_primitive &primitive) {
        rv_cv_frame_put(cv, &primitive);
    };

    const bool font_ok = addr_font_ != 0;
    const rv_ui_rect window = ui_window(static_cast<int>(screen_width_), static_cast<int>(screen_height_));
    const rv_ui_rect title_bar = ui_title_bar(window);
    const rv_ui_rect panel = ui_panel(window);
    const rv_ui_rect panel_inside{ panel.x + UI_BEVEL, panel.y + UI_BEVEL, panel.w - 2 * UI_BEVEL, panel.h - 2 * UI_BEVEL };

    // Hint buttons: the pair is centred in the window, on the button band.
    const std::string_view hint_labels[] = { UI_HINT_SOUND, UI_HINT_POWER };
    static_assert(static_cast<int>(std::size(hint_labels)) == UI_HINT_BUTTONS, "the frame capacity bound counts hint buttons");
    int button_w[std::size(hint_labels)];
    int total_w = RV_DMAIN_POST_BUTTON_GAP * (static_cast<int>(std::size(hint_labels)) - 1);
    for (std::size_t i = 0; i < std::size(hint_labels); ++i) {
        button_w[i] = ui_ink_width(hint_labels[i], 1) + 2 * theme::rv_theme_button_pad_x;
        total_w += button_w[i];
    }
    rv_ui_rect buttons[std::size(hint_labels)];
    int button_x = window.x + (window.w - total_w) / 2;
    for (std::size_t i = 0; i < std::size(hint_labels); ++i) {
        buttons[i] = rv_ui_rect{ button_x, ui_band_top(window), button_w[i], theme::rv_theme_button_h };
        button_x += button_w[i] + RV_DMAIN_POST_BUTTON_GAP;
    }
    // What does not fit inside the window face is dropped, not drawn outside it.
    const bool buttons_fit = total_w <= window.w - 2 * UI_BEVEL;
    const std::size_t button_count = buttons_fit ? std::size(hint_labels) : 0;
    if (!buttons_fit && !hint_warned_) {
        hint_warned_ = true;
        RV_LOG_WARN("post", "screen {}x{} is too small, the hint buttons are not drawn", screen_width_, screen_height_);
    }

    ui_plate_raised(theme::rv_theme_window, window, UI_DEPTH_WINDOW, file);
    file(make_bar(static_cast<float>(title_bar.x),
        static_cast<float>(title_bar.y),
        static_cast<float>(title_bar.w),
        static_cast<float>(title_bar.h),
        ui_color(theme::rv_theme_dark),
        UI_DEPTH_ON_WINDOW));
    ui_plate_sunken(font_ok ? theme::rv_theme_inset : theme::rv_theme_error, panel, UI_DEPTH_ON_WINDOW, file);
    for (std::size_t i = 0; i < button_count; ++i) {
        ui_plate_raised(theme::rv_theme_button, buttons[i], UI_DEPTH_ON_WINDOW, file);
    }

    // No font, no text anywhere.
    if (!font_ok) {
        return;
    }

    static_assert(FONT_INK_COUNT == 4, "styles below follow rv_font_ink");
    const rv_pdklib::rv_font_style styles[FONT_INK_COUNT] = {
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_[FONT_INK_TEXT], UI_DEPTH_TEXT, 1),
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_[FONT_INK_BRIGHT], UI_DEPTH_TEXT, 1),
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_[FONT_INK_DISABLED], UI_DEPTH_TEXT, 1),
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_[FONT_INK_ERROR], UI_DEPTH_TEXT, 1),
    };

    // Title bar: the name in bold on the left, the tray state on the right.
    const int title_y = window.y + theme::rv_theme_title_text_y;
    ui_text_bold(styles[FONT_INK_BRIGHT], title_bar, window.x + theme::rv_theme_title_text_x, title_y, UI_TITLE, file);
    ui_text(styles[FONT_INK_TEXT],
        title_bar,
        window.x + window.w - theme::rv_theme_title_text_x - ui_ink_width(UI_NO_DISC, 1),
        title_y,
        UI_NO_DISC,
        file);

    for (std::size_t i = 0; i < button_count; ++i) {
        ui_text(styles[FONT_INK_TEXT],
            buttons[i],
            buttons[i].x + theme::rv_theme_button_pad_x,
            buttons[i].y + theme::rv_theme_button_text_y,
            hint_labels[i],
            file);
    }

    const int text_x = panel.x + theme::rv_theme_panel_text_x;
    const auto line_y = [&panel](int line) {
        return panel.y + UI_PANEL_PAD_Y + line * theme::rv_theme_line_pitch;
    };

    // The budget line. Saying Virtual out loud matters: none of this is what the host has, all of it is
    // what the fantasy machine is defined to have, and the pools really do refuse past the line.
    char vram[RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE];
    char sram[RV_DMAIN_POST_MEMORY_LABEL_BUFFER_SIZE];
    char budget[RV_DMAIN_POST_BUDGET_BUFFER_SIZE];
    format_bytes(vram, sizeof(vram), video_memory_size_);
    format_bytes(sram, sizeof(sram), sound_memory_size_);
    std::snprintf(budget,
        sizeof(budget),
        UI_BUDGET_FORMAT,
        static_cast<long long>(screen_width_),
        static_cast<long long>(screen_height_),
        vram,
        static_cast<long long>(voice_count_),
        sram);
    const rv_post_piece budget_piece[] = { { budget, &styles[FONT_INK_TEXT] } };
    draw_line(budget_piece, panel_inside, text_x, line_y(0), file);

    // Verdict -> text and ink; a new verdict without a row stops compiling.
    enum rv_verdict : int {
        VERDICT_OK,
        VERDICT_ERR,
        VERDICT_NA,
        VERDICT_COUNT
    };
    struct rv_verdict_row {
        std::string_view text;
        rv_font_ink ink;
    };
    static constexpr rv_verdict_row verdict_rows[] = {
        { UI_VERDICT_OK, FONT_INK_TEXT },
        { UI_VERDICT_ERR, FONT_INK_ERROR },
        { UI_VERDICT_NA, FONT_INK_DISABLED },
    };
    static_assert(std::size(verdict_rows) == VERDICT_COUNT, "one row per verdict");

    // absent is a legal "nothing there", which is not a failure.
    const auto verdict = [](bool ok, bool absent) {
        return absent ? VERDICT_NA : (ok ? VERDICT_OK : VERDICT_ERR);
    };
    const rv_verdict probes[RV_DMAIN_POST_PROBE_COUNT] = {
        verdict(texture_result_ == RV_OK && idx4_result_ == RV_OK, false),
        verdict(font_result_ == RV_OK, false),
        verdict(beep_ok_, false),
        verdict(asset_ok_, asset_bytes_ == 0),
        verdict(card_ok_, false),
        verdict(drive_rejects_paths_, false),
    };

    // Three probes per row, in label order. The gap before a label is part of its bright piece.
    char labels[RV_DMAIN_POST_PROBE_COUNT][RV_DMAIN_POST_LABEL_BUFFER_SIZE];
    for (int row = 0; row < RV_DMAIN_POST_PROBE_ROWS; ++row) {
        rv_post_piece pieces[RV_DMAIN_POST_PROBES_PER_ROW * RV_DMAIN_POST_PIECES_PER_PROBE];
        for (int col = 0; col < RV_DMAIN_POST_PROBES_PER_ROW; ++col) {
            const int index = row * RV_DMAIN_POST_PROBES_PER_ROW + col;
            const std::string_view label = UI_PROBE_LABELS[index];
            // snprintf returns the untruncated length; the view comes from the terminated buffer instead.
            std::snprintf(labels[index],
                sizeof(labels[index]),
                "%.*s%.*s%.*s",
                static_cast<int>(col == 0 ? 0 : UI_PROBE_GAP.size()),
                UI_PROBE_GAP.data(),
                static_cast<int>(label.size()),
                label.data(),
                static_cast<int>(UI_LABEL_END.size()),
                UI_LABEL_END.data());
            const rv_verdict_row &shown = verdict_rows[probes[index]];
            pieces[col * RV_DMAIN_POST_PIECES_PER_PROBE] = { labels[index], &styles[FONT_INK_BRIGHT] };
            pieces[col * RV_DMAIN_POST_PIECES_PER_PROBE + 1] = { shown.text, &styles[shown.ink] };
        }
        draw_line(pieces, panel_inside, text_x, line_y(RV_DMAIN_POST_PROBE_LINE_FIRST + row), file);
    }

    char pads[RV_DMAIN_POST_PADS_BUFFER_SIZE];
    std::snprintf(pads,
        sizeof(pads),
        UI_PADS_FORMAT,
        static_cast<long long>(pads_connected_),
        static_cast<unsigned long>(boot_count_));
    const rv_post_piece pads_piece[] = { { pads, &styles[FONT_INK_TEXT] } };
    draw_line(pads_piece, panel_inside, text_x, line_y(RV_DMAIN_POST_PADS_LINE), file);
}

} // namespace rv_service
