// Candidate Playtest status: two lines, no Run Candidate (Release Controls' Run does that), no Run in the Burn shelf.

#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

#include <string>

using namespace rv_editor;

namespace
{
bool has(const std::string &s, const char *part)
{
    return s.find(part) != std::string::npos;
}
} // namespace

RV_EDITOR_TEST(rv_editor_test_game_status_two_lines)
{
    const auto l = rv_editor_game_status_compose(320, 240, "Fit 2.00x", "Stopped.", "");
    RV_EDITOR_CHECK(l.size_scale == "320x240  Fit 2.00x");
    RV_EDITOR_CHECK(l.state == "Stopped.");
}

RV_EDITOR_TEST(rv_editor_test_game_status_state_carries_stale)
{
    const auto l = rv_editor_game_status_compose(320, 240, "Fit 2.00x", "Stopped.", "stale: the last frame of session 3, stopped");
    RV_EDITOR_CHECK(l.size_scale == "320x240  Fit 2.00x");
    RV_EDITOR_CHECK(has(l.state, "Stopped."));
    RV_EDITOR_CHECK(has(l.state, "stale: the last frame of session 3, stopped"));
    RV_EDITOR_CHECK(!has(l.size_scale, "stale"));
}

RV_EDITOR_TEST(rv_editor_test_game_status_candidate_texts_never_name_run_candidate)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const std::string none = rv_editor_text("pane_game.no_candidate");
    const std::string image = rv_editor_text("pane_game.candidate_image");
    RV_EDITOR_CHECK(!has(none, "Run Candidate"));
    RV_EDITOR_CHECK(!has(image, "Run Candidate"));
    for (const std::string &state : { none, image }) {
        const auto l = rv_editor_game_status_compose(320, 240, "Fit 1.00x", state, "stale: session 1");
        RV_EDITOR_CHECK(!l.size_scale.empty());
        RV_EDITOR_CHECK(!has(l.size_scale, "Run Candidate"));
        RV_EDITOR_CHECK(!has(l.state, "Run Candidate"));
    }
}

RV_EDITOR_TEST(rv_editor_test_game_shelf_run_button_only_outside_release_view)
{
    RV_EDITOR_CHECK(rv_editor_game_shelf_shows_run(false));
    RV_EDITOR_CHECK(!rv_editor_game_shelf_shows_run(true));
}
