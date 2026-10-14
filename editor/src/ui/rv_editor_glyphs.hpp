#pragma once

// Two-letter codes drawn on square icon buttons: one code per concept, colours
// stay at call sites (theme members, not constexpr here).

namespace rv_editor_glyph
{

// actions
constexpr const char *build = "Bd";
constexpr const char *run = "Rn";
constexpr const char *pause = "Ps";
constexpr const char *step_frame = "Sf";
constexpr const char *stop = "Sp";
constexpr const char *reload = "Rl";
constexpr const char *more = "Mo";
constexpr const char *run_in_player = "Pl";
constexpr const char *export_ = "Ex";
constexpr const char *new_ = "Nw";
constexpr const char *new_folder = "Nd";
constexpr const char *delete_ = "Dl";
constexpr const char *rename = "Mv";
constexpr const char *refresh = "Rf";
constexpr const char *recent = "Rc";
constexpr const char *open = "Op";
constexpr const char *settings = "Se";
constexpr const char *help = "Hp";
constexpr const char *manual = "Mn";
constexpr const char *mark_moment = "Mk";
constexpr const char *capture_frame = "Cp";
constexpr const char *report_issue = "Is";
constexpr const char *restart = "Rs";

// file types
constexpr const char *folder = "Fd";
constexpr const char *link = "Ln";
constexpr const char *lua = "Lu";
constexpr const char *cpp = "Cc";
constexpr const char *header = "Hh";
constexpr const char *toml = "Tm";
constexpr const char *disc_toml = "Mf";
constexpr const char *image = "Im";
constexpr const char *sound = "Sn";
constexpr const char *text = "Tx";
constexpr const char *other_file = "Fi";

} // namespace rv_editor_glyph
