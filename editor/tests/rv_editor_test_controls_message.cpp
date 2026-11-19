// Runtime Controls: the message line is one short phrase with the full text for the tooltip, and the
// facts line never names a target, the run state or the last run.

#include <string>

#include "panes/rv_editor_pane_controls.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

namespace
{

bool has(const std::string &s, const char *part)
{
    return s.find(part) != std::string::npos;
}

constexpr size_t brief_max = 60;
const std::string long_detail(300, 'x');

} // namespace

RV_EDITOR_TEST(rv_editor_test_controls_message_none)
{
    const auto m = rv_editor_controls_message({});
    RV_EDITOR_CHECK(m.brief.empty());
    RV_EDITOR_CHECK(m.full.empty());
}

RV_EDITOR_TEST(rv_editor_test_controls_message_disc_cannot_reload)
{
    rv_editor_controls_message_input in;
    in.offer_build_restart = true;
    in.disc_cannot_reload = true;
    in.why_not_reload = "should be ignored";
    const auto m = rv_editor_controls_message(in);
    RV_EDITOR_CHECK(m.brief == "Can't reload this disc: Restart (loses state)");
    RV_EDITOR_CHECK(m.brief.size() <= brief_max);
    RV_EDITOR_CHECK(!m.full.empty());
    RV_EDITOR_CHECK(
        m.full == std::string(rv_editor_text("panes.disc_cannot_reload")) + " " + rv_editor_text("panes.restart_loses_state"));
}

RV_EDITOR_TEST(rv_editor_test_controls_message_build_restart_reason)
{
    rv_editor_controls_message_input in;
    in.offer_build_restart = true;
    in.why_not_reload = "The change adds a new asset: " + long_detail;
    const auto m = rv_editor_controls_message(in);
    RV_EDITOR_CHECK(!m.brief.empty());
    RV_EDITOR_CHECK(m.brief.size() <= brief_max);
    RV_EDITOR_CHECK(has(m.full, "The change adds a new asset"));
    RV_EDITOR_CHECK(has(m.full, "Restart"));
}

RV_EDITOR_TEST(rv_editor_test_controls_message_each_state_is_short)
{
    rv_editor_controls_message_input hung, gone, uncertain, detail;
    hung.force_stop = true;
    hung.hung = true;
    gone.force_stop = true;
    uncertain.uncertain = true;
    detail.detail = long_detail;
    for (const auto *in : { &hung, &gone, &uncertain, &detail }) {
        const auto m = rv_editor_controls_message(*in);
        RV_EDITOR_CHECK(!m.brief.empty());
        RV_EDITOR_CHECK(m.brief.size() <= brief_max);
        RV_EDITOR_CHECK(m.brief.find('\n') == std::string::npos);
        RV_EDITOR_CHECK(!m.full.empty());
    }
    // The full text keeps what the short one drops.
    RV_EDITOR_CHECK(has(rv_editor_controls_message(detail).full, long_detail.c_str()));
    RV_EDITOR_CHECK(has(rv_editor_controls_message(hung).full, "did not end"));
    RV_EDITOR_CHECK(has(rv_editor_controls_message(gone).full, "channel is gone"));
    RV_EDITOR_CHECK(has(rv_editor_controls_message(uncertain).full, "went unanswered"));
}

RV_EDITOR_TEST(rv_editor_test_controls_message_priority)
{
    rv_editor_controls_message_input in;
    in.force_stop = true;
    in.hung = true;
    in.uncertain = true;
    in.detail = "DETAIL";
    in.offer_build_restart = true;
    in.disc_cannot_reload = true;
    RV_EDITOR_CHECK(has(rv_editor_controls_message(in).full, "did not end"));
    in.force_stop = false;
    RV_EDITOR_CHECK(has(rv_editor_controls_message(in).full, "went unanswered"));
    in.uncertain = false;
    RV_EDITOR_CHECK(has(rv_editor_controls_message(in).full, "DETAIL"));
    in.detail.clear();
    RV_EDITOR_CHECK(has(rv_editor_controls_message(in).full, "cannot reload"));
}

RV_EDITOR_TEST(rv_editor_test_controls_no_last_run_in_any_message)
{
    rv_editor_controls_message_input in;
    in.offer_build_restart = true;
    in.disc_cannot_reload = true;
    in.uncertain = true;
    in.force_stop = true;
    for (bool hung : { false, true }) {
        in.hung = hung;
        const auto m = rv_editor_controls_message(in);
        RV_EDITOR_CHECK(!has(m.brief, "Last run") && !has(m.full, "Last run"));
        RV_EDITOR_CHECK(!has(m.brief, "Target") && !has(m.full, "Target"));
    }
}

RV_EDITOR_TEST(rv_editor_test_controls_dead_texts_removed)
{
    for (const char *id : { "panes.target_build",
             "panes.target_nothing_built",
             "panes.last_run",
             "panes.facts_live",
             "panes.profile",
             "panes.run_config_tooltip" }) {
        // An unknown id comes back as itself.
        RV_EDITOR_CHECK(std::string(rv_editor_text(id)) == id);
    }
}
