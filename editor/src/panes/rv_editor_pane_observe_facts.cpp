#include "panes/rv_editor_pane_observe.hpp"

#include <string>

#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_facts.hpp"

namespace rv_editor
{

// The rows of the facts: the session and what the console said of itself.
void rv_editor_observe_facts(const rv_editor_session &s, const rv_editor_theme &theme)
{
    const rv_editor_session_facts &f = s.facts();
    const auto fact = [&theme](const char *key, const std::string &value) {
        rv_editor_facts_row(key, value.c_str(), theme);
    };
    const auto num = std::to_string(s.number());
    const auto state = rv_editor_run_state_name(s.state());
    const auto start_at = rv_editor_clock(s.started_at());
    fact(rv_editor_text("pane_observe.fact_session"),
        rv_editor_text_format("pane_observe.fact_session_value", std::make_format_args(num, state, start_at)));
    const auto frame = std::to_string(s.frame());
    const char *frame_key =
        s.state() == rv_editor_run_state::paused ? "pane_observe.fact_frame_paused" : "pane_observe.fact_frame_running";
    fact(rv_editor_text("pane_observe.fact_frame"), rv_editor_text_format(frame_key, std::make_format_args(frame)));
    const char *disc_key = f.medium == medium_live ? "pane_observe.fact_disc_directory" : "pane_observe.fact_disc_image";
    fact(rv_editor_text("pane_observe.fact_disc"), rv_editor_text_format(disc_key, std::make_format_args(f.disc)));
    const auto &pdk = f.pdk;
    fact(rv_editor_text("pane_observe.fact_code_hash"),
        rv_editor_text_format("pane_observe.fact_code_hash_value", std::make_format_args(f.code_hash, pdk)));
    if (f.lua_budget > 0) {
        const auto rev = std::to_string(f.revision);
        std::string entry_val;
        if (f.revision != f.first_revision) {
            const auto first_rev = std::to_string(f.first_revision);
            entry_val = rv_editor_text_format("pane_observe.fact_entry_reloaded", std::make_format_args(rev, first_rev));
        } else {
            entry_val = rv_editor_text_format("pane_observe.fact_entry_as_loaded", std::make_format_args(rev));
        }
        fact(rv_editor_text("pane_observe.fact_entry_script"), entry_val);
        const auto lua_used = std::to_string(f.lua_used);
        const auto lua_budget = std::to_string(f.lua_budget);
        fact(rv_editor_text("pane_observe.fact_lua_memory"),
            rv_editor_text_format("pane_observe.fact_lua_memory_value", std::make_format_args(lua_used, lua_budget)));
    }
    const auto facts_at = rv_editor_clock(f.at);
    fact(rv_editor_text("pane_observe.fact_facts_from"),
        rv_editor_text_format("pane_observe.fact_facts_from_value", std::make_format_args(facts_at)));
}

} // namespace rv_editor
