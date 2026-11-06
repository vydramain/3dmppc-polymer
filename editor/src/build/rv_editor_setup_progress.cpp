// Reads the step ladder and the progress tails that `mppcburner setup` prints.

#include "build/rv_editor_setup_progress.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace rv_editor
{

namespace
{

// Ladder prefix "[n/N] label  detail", printed as "[%d/%d] %-11s %s" by rv_burner_print_step
// in pdk/tools/mppcburner/rv_burner_print.hpp.
constexpr std::string_view step_open = "[";
constexpr std::string_view step_count_sep = "/";
constexpr std::string_view step_close = "]";
constexpr std::string_view space = " ";
// Line ends dropped before the parse: the reader may keep the CR of a CRLF line.
constexpr std::string_view line_ends = "\r\n";

// Progress tails, written by report_share and report_unknown_size in
// pdk/tools/mppcburner/rv_burner_setup/rv_burner_setup.cpp.
constexpr std::string_view percent_sign = "%";
// Follows the percent in "<file>: <pct>% of <size>"; the size after it is never empty.
constexpr std::string_view size_of = " of ";
// Ends "<file>: size unknown".
constexpr std::string_view size_unknown_tail = ": size unknown";
// Separates the file name from the percent in "<file>: <pct>%".
constexpr std::string_view name_sep = ": ";
// The largest percent a progress line can carry.
constexpr int percent_max = 100;
// Permille of one percent, and of one whole step.
constexpr int permille_per_percent = 10;
constexpr int permille_per_step = 1000;

bool is_digit_char(char c)
{
    return '0' <= c && c <= '9';
}

// An unsigned decimal number that fits an int; false for an empty text, a non-digit or an overflow.
bool read_number(std::string_view text, int &value)
{
    if (text.empty() || !std::ranges::all_of(text, is_digit_char)) {
        return false;
    }
    return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc();
}

// One ladder prefix: the step and total of "[n/N]", the label, and the detail after the label.
struct ladder_line {
    int step = 0;
    int total = 0;
    std::string_view label;
    std::string_view detail;
};

// Splits "[n/N] label  detail" with 1 <= n <= N; false when the prefix or the label is not there.
bool split_ladder(std::string_view line, ladder_line &out)
{
    if (!line.starts_with(step_open)) {
        return false;
    }
    line.remove_prefix(step_open.size());
    const std::size_t count_at = line.find(step_count_sep);
    if (count_at == std::string_view::npos || !read_number(line.substr(0, count_at), out.step)) {
        return false;
    }
    line.remove_prefix(count_at + step_count_sep.size());
    const std::size_t close_at = line.find(step_close);
    if (close_at == std::string_view::npos || !read_number(line.substr(0, close_at), out.total)) {
        return false;
    }
    if (out.step < 1 || out.total < 1 || out.step > out.total) {
        return false;
    }
    line.remove_prefix(close_at + step_close.size());
    if (!line.starts_with(space)) {
        return false;
    }
    line.remove_prefix(space.size());
    // The label is padded with spaces to 11 columns; the detail starts after them.
    const std::size_t label_end = std::min(line.find(space), line.size());
    if (label_end == 0) {
        return false;
    }
    out.label = line.substr(0, label_end);
    out.detail = line.substr(label_end);
    out.detail.remove_prefix(std::min(out.detail.find_first_not_of(space), out.detail.size()));
    return true;
}

// Reads the in-step part from the detail's tail. A tail counts only by its full shape: ": size unknown",
// ": <pct>%" or ": <pct>% of <size>" at the end. Any other detail is plain text: part stays none.
// A detail with the ": <token>%" shape whose token is not a number 0-100 is rejected (false).
// `file` gets the name before the tail; it stays empty when the tail has no name.
bool read_part(std::string_view detail, rv_editor_setup_part &part, int &percent, std::string_view &file)
{
    if (detail.ends_with(size_unknown_tail)) {
        part = rv_editor_setup_part::size_unknown;
        file = detail.substr(0, detail.size() - size_unknown_tail.size());
        return true;
    }
    const std::size_t mark = detail.rfind(percent_sign);
    if (mark == std::string_view::npos) {
        return true;
    }
    const std::string_view after = detail.substr(mark + percent_sign.size());
    const bool sized = after.starts_with(size_of) && after.size() > size_of.size();
    if (!after.empty() && !sized) {
        return true;
    }
    const std::string_view head = detail.substr(0, mark);
    const std::size_t name_at = head.rfind(name_sep);
    if (name_at == std::string_view::npos) {
        return true;
    }
    const std::string_view token = head.substr(name_at + name_sep.size());
    if (token.find(space) != std::string_view::npos) {
        return true;
    }
    if (!read_number(token, percent) || percent > percent_max) {
        return false;
    }
    part = rv_editor_setup_part::share;
    file = head.substr(0, name_at);
    return true;
}

} // namespace

bool rv_editor_setup_feed(const std::string &line, rv_editor_setup_progress &progress)
{
    std::string_view text = line;
    const std::size_t kept = text.find_last_not_of(line_ends);
    text = kept == std::string_view::npos ? std::string_view() : text.substr(0, kept + 1);
    ladder_line parsed;
    if (!split_ladder(text, parsed)) {
        return false;
    }
    rv_editor_setup_part part = rv_editor_setup_part::none;
    int percent = 0;
    std::string_view file;
    if (!read_part(parsed.detail, part, percent, file)) {
        return false;
    }
    progress.step = parsed.step;
    progress.total = parsed.total;
    progress.label = std::string(parsed.label);
    progress.part = part;
    progress.percent = percent;
    progress.file = std::string(file);
    return true;
}

int rv_editor_setup_permille(const rv_editor_setup_progress &progress)
{
    if (progress.step < 1 || progress.step > progress.total) {
        return 0;
    }
    // Clamped, so a hand-filled percent cannot take the result outside 0-1000.
    const int in_step =
        rv_editor_setup_part::share == progress.part ? std::clamp(progress.percent, 0, percent_max) * permille_per_percent : 0;
    const std::int64_t done = static_cast<std::int64_t>(progress.step - 1) * permille_per_step + in_step;
    return static_cast<int>(done / progress.total);
}

} // namespace rv_editor
