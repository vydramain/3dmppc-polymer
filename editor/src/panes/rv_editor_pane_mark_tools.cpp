// The mark tools row: the note field and the small tiles Mark Moment, Capture Frame, Report Issue [and Restart].

#include "panes/rv_editor_pane_mark_tools.hpp"

#include <algorithm>
#include <limits>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// The draft of the row's one field; the add_mark request takes a copy of it.
char mark_draft[mark_note_field_size] = {};
// The marks count when a mark request took the draft; the draft clears once that request has run and
// the count grew. no_mark_sent: nothing is waiting.
constexpr size_t no_mark_sent = std::numeric_limits<size_t>::max();
size_t mark_count_at_send = no_mark_sent;
// The field never gets narrower than this many ems when the row is sized for a reserve.
constexpr float min_field_em = 10.0f;

// The row's tiles; their looks do not depend on the session.
struct rv_editor_mark_tiles {
    rv_editor_tile mark;
    rv_editor_tile capture;
    rv_editor_tile report;
    rv_editor_tile restart;
};

rv_editor_mark_tiles rv_editor_make_mark_tiles(const rv_editor_theme &theme)
{
    return { { rv_editor_text("pane_toolchest.mark_button"),
                 rv_editor_glyph::mark_moment,
                 theme.code_yellow,
                 rv_editor_text("pane_toolchest.mark_tooltip"),
                 nullptr },
        { rv_editor_text("pane_toolchest.capture_button"),
            rv_editor_glyph::capture_frame,
            theme.code_blue,
            rv_editor_text("pane_toolchest.capture_tooltip"),
            nullptr },
        { rv_editor_text("pane_toolchest.report_button"),
            rv_editor_glyph::report_issue,
            theme.code_red,
            rv_editor_text("pane_toolchest.report_tooltip"),
            nullptr },
        { rv_editor_text("pane_toolchest.restart_button"),
            rv_editor_glyph::restart,
            theme.code_green,
            rv_editor_text("pane_toolchest.restart_tooltip"),
            nullptr } };
}

float rv_editor_small_tiles_width(const rv_editor_mark_tiles &t, bool with_restart)
{
    // One gap before each small tile: the first is the gap after the field.
    const float tiles = rv_editor_tile_width(t.mark, rv_editor_tile_size::small) +
        rv_editor_tile_width(t.capture, rv_editor_tile_size::small) +
        rv_editor_tile_width(t.report, rv_editor_tile_size::small) +
        (with_restart ? rv_editor_tile_width(t.restart, rv_editor_tile_size::small) : 0.0f);
    return tiles + (with_restart ? 4.0f : 3.0f) * ImGui::GetStyle().ItemSpacing.x;
}

} // namespace

float rv_editor_mark_tools_min_width(const rv_editor_theme &theme, bool with_restart)
{
    return min_field_em * ImGui::GetFontSize() + rv_editor_small_tiles_width(rv_editor_make_mark_tiles(theme), with_restart);
}

std::vector<rv_editor_tile_rect>
rv_editor_mark_tools(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui, bool with_restart)
{
    rv_editor_session &s = app.session;
    const char *no_session = s.live() ? nullptr : rv_editor_text("pane_toolchest.no_session");
    const bool mark_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::add_mark);
    const bool mark_done = mark_count_at_send != no_mark_sent && !mark_pending;
    // A refused mark (the app logged why) leaves the draft for another try.
    if (mark_done && app.marks.size() > mark_count_at_send) {
        mark_draft[0] = '\0';
    }
    if (mark_done) {
        mark_count_at_send = no_mark_sent;
    }
    const char *mark_why = mark_pending ? rv_editor_text("app_why_not.request_pending") : no_session;
    const bool capture_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::capture_frame);
    const char *capture_why = capture_pending ? rv_editor_text("app_why_not.request_pending") : no_session;
    // The moment is kept before the form is: a frame, the keyboard back, then Findings.
    const char *no_report = s.number() != 0 ? nullptr : rv_editor_text("pane_toolchest.no_report");
    const bool report_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::report_problem);
    const char *report_why = report_pending ? rv_editor_text("app_why_not.request_pending") : no_report;
    // A restart in progress says so in place of the no-session reason.
    const bool restart_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::restart);
    const char *restart_why = restart_pending ? rv_editor_text("app_why_not.restart_pending") : no_session;
    const rv_editor_mark_tiles tiles = rv_editor_make_mark_tiles(theme);

    // The field takes what is left of the row after the small tiles and their gaps.
    const float field_w = std::max(1.0f, ImGui::GetContentRegionAvail().x - rv_editor_small_tiles_width(tiles, with_restart));
    ImGui::SetNextItemWidth(field_w);
    rv_editor_text_field("##mark_note", mark_draft, sizeof(mark_draft), theme, { {}, mark_pending });
    if (mark_draft[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            rv_editor_text("pane_toolchest.mark_placeholder"));
    }
    std::vector<rv_editor_tile_rect> rects;
    const auto track = [&rects] {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        rects.push_back({ min.x, min.y, max.x, max.y });
    };
    ImGui::SameLine();
    if (rv_editor_tile_button(tiles.mark, rv_editor_tile_size::small, theme, { rv_editor_look::live, mark_why })) {
        const rv_editor_pane_request request = { rv_editor_pane_request_kind::add_mark, rv_editor_tile_none, {}, mark_draft };
        if (rv_editor_app_request(app, request) == RV_OK) {
            mark_count_at_send = app.marks.size();
        }
    }
    track();
    ImGui::SameLine();
    if (rv_editor_tile_button(tiles.capture, rv_editor_tile_size::small, theme, { rv_editor_look::live, capture_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::capture_frame });
    }
    track();
    ImGui::SameLine();
    if (rv_editor_tile_button(tiles.report, rv_editor_tile_size::small, theme, { rv_editor_look::live, report_why })) {
        const bool queued = rv_editor_app_request(app, { rv_editor_pane_request_kind::report_problem }) == RV_OK;
        if (queued) {
            ui.requests.push_back({ .kind = rv_editor_ui_request_kind::release_keyboard });
        }
    }
    track();
    if (with_restart) {
        ImGui::SameLine();
        if (rv_editor_tile_button(tiles.restart, rv_editor_tile_size::small, theme, { rv_editor_look::live, restart_why })) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::restart });
        }
        track();
    }
    return rects;
}

} // namespace rv_editor
