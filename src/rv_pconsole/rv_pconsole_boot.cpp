// The start screen's stage: the window, the presses and the phase rule. The pictures are
// rv_pcboot_screen.cpp's; the loops that call this are per driver. No sleeping here: the caller paces.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <string_view>

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

// Opaque black, 0xAARRGGBB: the frame the page shows once the start screen is gone and no disc runs.
constexpr uint32_t RV_PCONSOLE_BOOT_BLACK = 0xFF000000u;

// A primary-button press inside the button of the starting view.
bool boot_button_hit(const rv_pcpointer &point, const rv_pcboot_rect &button)
{
    return point.inside && point.x >= button.x && point.x < button.x + button.w && point.y >= button.y &&
        point.y < button.y + button.h;
}

} // namespace

bool rv_pconsole::boot_window_shows()
{
    if (boot_window_ != boot_window_state::unopened) {
        return boot_window_ == boot_window_state::shown;
    }

    // Checked before any open: the frame-fd window remaps its memory when it opens.
    if (platform_.window().embedded()) {
        boot_window_ = boot_window_state::absent;
        return false;
    }

    const int64_t opened =
        platform_.window().open(rv_pctext("boot.title"), cv_->screen_width(), cv_->screen_height(), params_.scale);
    if (0 > opened) {
        RV_LOG_WARN("pconsole", "start screen did not come up (rc {}), continuing without it", opened);
        boot_window_ = boot_window_state::absent;
        return false;
    }

    // open() succeeds without a window when none was wanted: there is nothing to show on.
    boot_window_ = platform_.window().presenting() ? boot_window_state::shown : boot_window_state::absent;
    return boot_window_ == boot_window_state::shown;
}

void rv_pconsole::boot_screen_during_boot()
{
    if (!boot_window_shows()) {
        return;
    }
    // No pump and no input here: the stage owns both, and begin drops the presses from before it.
    boot_state_ = rv_pcboot_state::starting;
    // show() logs its own warn when a view cannot be drawn; the boot goes on either way.
    static_cast<void>(boot_screen_show());
}

bool rv_pconsole::boot_screen_begin()
{
    if (!boot_window_shows()) {
        return false;
    }

    // Presses from before the screen was up belong to nobody; they are dropped once, here.
    static_cast<void>(platform_.window().consume_presses());
    static_cast<void>(platform_.gamepads().consume_button_presses());
    static_cast<void>(platform_.window().consume_pause_requests());
    static_cast<void>(platform_.window().consume_mouse());

    const rv_pcboot_state first = boot_journal_.has_error() ? rv_pcboot_state::error :
        boot_journal_.has_warning()                         ? rv_pcboot_state::warning :
                                                              rv_pcboot_state::starting;
    boot_state_ = first;
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

    switch (boot_state_) {
    case rv_pcboot_state::starting: {
        const rv_pcboot_rect button =
            rv_pcboot_button_rect(cv_->screen_width(), cv_->screen_height(), rv_pcboot_state::starting);
        // The press that stops is used up; it does not also continue.
        if (presses.escape > 0 || boot_button_hit(presses.last_click, button)) {
            boot_state_ = rv_pcboot_state::stopped;
            break;
        }
        if (std::chrono::steady_clock::now() - boot_started_ >= RV_PCONSOLE_BOOT_SCREEN_HOLD) {
            return boot_screen_result::done;
        }
        break;
    }
    case rv_pcboot_state::warning:
    case rv_pcboot_state::stopped:
    case rv_pcboot_state::error:
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
    int64_t unfit = 0;
    const int built = rv_pcboot_screen_build(boot_picture_, width, height, boot_journal_, boot_state_, &unfit);
    if (built != RV_OK) {
        RV_LOG_WARN("pconsole", "start screen not drawn (rc {}), the boot goes on without it", built);
        return built;
    }

    // The count is meaningful only with RV_OK, so the warning waits for it.
    boot_screen_warn_unfit(unfit);
    platform_.window().present(boot_picture_.data());
    return RV_OK;
}

void rv_pconsole::boot_screen_warn_unfit(int64_t unfit)
{
    // Names by rv_pcboot_state value. "at least": the starting view stays up while the journal still grows.
    constexpr std::string_view VIEW_NAMES[] = { "starting view", "warning view", "stopped view", "error view" };
    static_assert(std::size(VIEW_NAMES) == static_cast<std::size_t>(rv_pcboot_state::error) + 1,
        "one view name per rv_pcboot_state value");

    const uint32_t bit = 1u << static_cast<uint32_t>(boot_state_);
    const bool warned = (boot_unfit_warned_ & bit) != 0;
    if (unfit <= 0 || warned) {
        return;
    }
    boot_unfit_warned_ |= bit;
    RV_LOG_WARN("pconsole",
        "the start screen at {}x{} is too small: at least {} items of the {} are not drawn",
        cv_->screen_width(),
        cv_->screen_height(),
        unfit,
        VIEW_NAMES[static_cast<std::size_t>(boot_state_)]);
}

void rv_pconsole::boot_screen_present_black()
{
    // Not presenting: the window shows nothing, so there is no frame to blacken.
    if (!platform_.window().presenting()) {
        return;
    }
    const int64_t width = cv_->screen_width();
    const int64_t height = cv_->screen_height();
    // No frame without a size; the guard also keeps the size_t cast below from wrapping.
    if (width <= 0 || height <= 0) {
        try {
            RV_LOG_WARN("pconsole", "the black frame was not made: the screen {}x{} has no size", width, height);
        } catch (const std::bad_alloc &) {
            // The reason is lost; the page keeps its last picture either way.
        }
        return;
    }

    const auto count = static_cast<std::size_t>(width * height);
    try {
        boot_picture_.assign(count, RV_PCONSOLE_BOOT_BLACK);
    } catch (const std::bad_alloc &) {
        try {
            RV_LOG_WARN("pconsole", "the black frame was not made (out of memory); the page keeps its last picture");
        } catch (const std::bad_alloc &) {
            // The reason is lost; the page keeps its last picture either way.
        }
        return;
    }
    platform_.window().present(boot_picture_.data());
}

} // namespace rv_3dmppc
