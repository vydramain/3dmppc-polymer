// The start screen's stage: the window, the presses and the phase rule. The pictures are
// rv_pcboot_screen.cpp's; the loops that call this are per driver. No sleeping here: the caller paces.
#include <chrono>
#include <cstdint>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcboot_screen.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pctexts.hpp"

namespace rv_3dmppc
{

namespace
{

// How long the starting view stays when nothing is wrong: long enough to read its lines,
// short enough not to slow a clean boot. Counted from begin, not in frames: web frames are not evenly paced.
constexpr auto RV_PCONSOLE_BOOT_SCREEN_HOLD = std::chrono::seconds(1);

// A primary-button press inside the button of the starting view.
bool boot_button_hit(const rv_pcpointer &point, const rv_pcboot_rect &button)
{
    return point.inside && point.x >= button.x && point.x < button.x + button.w && point.y >= button.y &&
        point.y < button.y + button.h;
}

} // namespace

bool rv_pconsole::boot_screen_begin()
{
    // Checked before any open: the frame-fd window remaps its memory when it opens.
    if (platform_.window().embedded()) {
        return false;
    }

    const int64_t opened =
        platform_.window().open(rv_pctext("boot.title"), cv_->screen_width(), cv_->screen_height(), params_.scale);
    if (0 > opened) {
        RV_LOG_WARN("pconsole", "start screen did not come up (rc {}), continuing without it", opened);
        return false;
    }

    // open() succeeds without a window when none was wanted: there is nothing to show on.
    if (!platform_.window().presenting()) {
        return false;
    }

    // Presses from before the screen was up belong to nobody; they are dropped once, here.
    static_cast<void>(platform_.window().consume_presses());
    static_cast<void>(platform_.gamepads().consume_button_presses());
    static_cast<void>(platform_.window().consume_pause_requests());
    static_cast<void>(platform_.window().consume_mouse());

    boot_phase_ = boot_journal_.has_warning() ? boot_screen_phase::report : boot_screen_phase::starting;
    boot_started_ = std::chrono::steady_clock::now();
    return boot_screen_show() == RV_OK;
}

rv_pconsole::boot_screen_result rv_pconsole::boot_screen_step()
{
    platform_.pump();
    if (platform_.quit_requested()) {
        RV_LOG_INFO("pconsole", "shutdown requested on the start screen");
        return boot_screen_result::quit;
    }

    const rv_pcpresses presses = platform_.window().consume_presses();
    const uint32_t pad_presses = platform_.gamepads().consume_button_presses();
    // Drained every pass, unused: the disc must not start paused or with a mouse delta from this stage.
    static_cast<void>(platform_.window().consume_pause_requests());
    static_cast<void>(platform_.window().consume_mouse());

    const rv_pcboot_rect button = rv_pcboot_button_rect(cv_->screen_width(), cv_->screen_height());
    switch (boot_phase_) {
    case boot_screen_phase::starting:
        // The presses that switch the view are used up; they do not also continue.
        if (presses.escape > 0 || boot_button_hit(presses.last_click, button)) {
            boot_phase_ = boot_screen_phase::report;
            break;
        }
        if (std::chrono::steady_clock::now() - boot_started_ >= RV_PCONSOLE_BOOT_SCREEN_HOLD) {
            return boot_screen_result::done;
        }
        break;
    case boot_screen_phase::report:
        if (presses.keys > 0 || presses.mouse_buttons > 0 || pad_presses > 0) {
            return boot_screen_result::done;
        }
        break;
    }

    // A picture that cannot be drawn ends the stage; the boot is not held up by it.
    if (boot_screen_show() != RV_OK) {
        return boot_screen_result::done;
    }
    return boot_screen_result::running;
}

int rv_pconsole::boot_screen_show()
{
    const int64_t width = cv_->screen_width();
    const int64_t height = cv_->screen_height();
    int built = RV_ERR_INVAL;
    switch (boot_phase_) {
    case boot_screen_phase::starting:
        built = rv_pcboot_screen_build(boot_picture_, width, height, boot_journal_);
        break;
    case boot_screen_phase::report:
        built = rv_pcboot_report_build(boot_picture_, width, height, boot_journal_);
        break;
    }
    if (built != RV_OK) {
        RV_LOG_WARN("pconsole", "start screen not drawn (rc {}), the boot goes on without it", built);
        return built;
    }

    platform_.window().present(boot_picture_.data());
    return RV_OK;
}

} // namespace rv_3dmppc
