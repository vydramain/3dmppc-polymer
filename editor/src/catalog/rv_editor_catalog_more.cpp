// The catalog's sections on focus and keys, scrolling and overflow, code text,
// the game frame, catalog cells and drop pockets, fonts, contrast and the
// thumbwheel: each drawn with the widget the editor uses, on fixed data (CAT-02).

#include "catalog/rv_editor_catalog.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "font/rv_editor_font.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "panes/rv_editor_panes.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_thumbwheel.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_catalog_more_data
{
    bool checks[3] = { false, true, false };
    int radio = 1;
    char pocket[64] = {};
    float wheel = 0.0f;
    char editable[32] = "editable";
    char locked[32] = "read-only";
};

rv_editor_catalog_more_data rv_editor_catalog_more;

// The test frame: 320x240 cells of 20 px in two tones, a white border and a centre cross.
void rv_editor_catalog_frame(ImDrawList *dl, ImVec2 p0, float scale, bool stale, const rv_editor_theme &t)
{
    const ImU32 alpha = stale ? 0x60000000u : 0xff000000u;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 16; ++x) {
            const ImU32 c = ((x + y) % 2 == 0 ? 0x00604030u : 0x00306080u) | alpha;
            dl->AddRectFilled(ImVec2(p0.x + x * 20 * scale, p0.y + y * 20 * scale),
                ImVec2(p0.x + (x + 1) * 20 * scale, p0.y + (y + 1) * 20 * scale), c);
        }
    }
    dl->AddRect(p0, ImVec2(p0.x + 320 * scale, p0.y + 240 * scale), 0x00ffffffu | alpha);
    dl->AddLine(ImVec2(p0.x + 160 * scale, p0.y), ImVec2(p0.x + 160 * scale, p0.y + 240 * scale), 0x00ffffffu | alpha);
    dl->AddLine(ImVec2(p0.x, p0.y + 120 * scale), ImVec2(p0.x + 320 * scale, p0.y + 120 * scale), 0x00ffffffu | alpha);
    (void)t;
}

} // namespace

void rv_editor_catalog_keys(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    ImGui::TextWrapped("Tab and Shift+Tab walk these; Space toggles a box, the arrows choose a diamond; the dotted "
                       "ring marks the one with the keyboard.");
    const char *labels[] = { "Mute", "Start Paused", "Snap (disabled)" };
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        rv_editor_checkbox(labels[i], &d.checks[i], theme,
            { rv_editor_look::live, i == 2 ? "Disabled: shows the reason on hover" : nullptr });
    }
    const char *radios[] = { "Select", "Move", "Rotate", "Scale" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_radio(radios[i], d.radio == i, theme)) {
            d.radio = i;
        }
    }
}

void rv_editor_catalog_overflow(const rv_editor_theme &theme)
{
    // A scrolled region with the editor's own bars, both ways.
    rv_editor_scroll_begin("##cat_scroll", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 4.0f), true,
        ImGuiChildFlags_Borders);
    for (int i = 1; i <= 12; ++i) {
        ImGui::Text("%02d  a line long enough to need the horizontal bar as well: scenes/level-%02d.scene.toml, "
                    "assets/textures/very-long-texture-name-%02d.png, assets/sounds/very-long-sound-name-%02d.pcm, "
                    "scripts/very-long-module-name-%02d.lua",
            i, i, i, i, i);
    }
    rv_editor_scroll_end(theme);
    // The transport in a narrow strip: what does not fit goes behind More.
    ImGui::TextUnformatted("Transport at 220 px:");
    ImGui::BeginChild("##cat_narrow", ImVec2(220.0f, ImGui::GetFrameHeight() * 3.2f), ImGuiChildFlags_Borders);
    const rv_editor_transport_state state{ nullptr, "Already running", nullptr, "Pause first", nullptr, nullptr, false };
    rv_editor_transport_bar(state, theme);
    ImGui::EndChild();
}

void rv_editor_catalog_code(const rv_editor_theme &theme)
{
    // The code font and the colours the code tile's nvim draws with; the tile itself needs a running nvim.
    ImGui::TextDisabled("The code tile is nvim; this shows its font and colours on fixed text.");
    const float w = ImGui::GetContentRegionAvail().x;
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 p0 = rv_editor_catalog_reserve(ImVec2(w, line * 7.5f));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + line * 7.5f), rv_editor_col(theme.code_base));
    rv_editor_font_code_push();
    const float cl = ImGui::GetTextLineHeight() + 2.0f;
    struct piece
    {
        const char *text;
        uint32_t color;
    };
    const piece rows[][4] = {
        { { "local ", theme.code_magenta }, { "M", theme.code_text }, { " = {}", theme.code_text }, { "", 0 } },
        { { "function ", theme.code_magenta }, { "M.frame_update", theme.code_blue }, { "(dt)", theme.code_text },
            { "", 0 } },
        { { "  -- ", theme.code_subtext }, { "\xd0\xba\xd0\xb0\xd0\xb4\xd1\x80: Cyrillic in a comment", theme.code_subtext },
            { "", 0 }, { "", 0 } },
        { { "  state.x = state.x + ", theme.code_text }, { "1.5", theme.code_yellow }, { " * dt", theme.code_text },
            { "", 0 } },
        { { "#include ", theme.code_magenta }, { "\"pdk/rv_pdko.h\"", theme.code_green }, { "", 0 }, { "", 0 } },
        { { "constexpr int32_t ", theme.code_magenta }, { "DEPTH", theme.code_text },
            { " = 400; int broken =", theme.code_text }, { "", 0 } },
    };
    for (int r = 0; r < 6; ++r) {
        float x = p0.x + 36.0f;
        const float y = p0.y + 4.0f + r * cl;
        char number[8];
        std::snprintf(number, sizeof(number), "%2d", r + 1);
        dl->AddText(ImVec2(p0.x + 4.0f, y), rv_editor_col(theme.code_subtext), number);
        if (r == 3) {
            // The selection and the cursor.
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 80.0f, y + cl), rv_editor_col(theme.code_surface));
        }
        for (const piece &p : rows[r]) {
            if (p.text[0] == '\0') {
                continue;
            }
            dl->AddText(ImVec2(x, y), rv_editor_col(p.color), p.text);
            x += ImGui::CalcTextSize(p.text).x;
        }
        if (r == 5) {
            // A build diagnostic under its place.
            dl->AddLine(ImVec2(p0.x + 36.0f, y + cl - 1.0f), ImVec2(x, y + cl - 1.0f), rv_editor_col(theme.code_red), 1.0f);
            dl->AddText(ImVec2(x + 12.0f, y), rv_editor_col(theme.code_red), "error: expected expression");
        }
        if (r == 3) {
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 2.0f, y + cl), rv_editor_col(theme.code_text));
        }
    }
    rv_editor_font_code_pop();
}

void rv_editor_catalog_game(const rv_editor_theme &theme)
{
    // The Game tile's own placement on a 320x240 test frame, in two areas and two modes, and a stale frame.
    const float h = ImGui::GetFrameHeight() * 7.0f;
    const struct
    {
        const char *label;
        rv_editor_game_scale mode;
        float w;
        bool stale;
    } cases[] = { { "Fit", rv_editor_game_scale::fit, h * 1.9f, false }, { "Integer", rv_editor_game_scale::integer,
                      h * 1.9f, false },
        { "Fit, stale", rv_editor_game_scale::fit, h * 1.4f, true } };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (const auto &c : cases) {
        ImGui::BeginGroup();
        const rv_editor_game_view view = rv_editor_game_place(320, 240, c.w, h, c.mode);
        ImGui::Text("%s %.2fx%s", c.label, view.scale, c.stale ? ": the last frame of an ended session" : "");
        const ImVec2 p0 = rv_editor_catalog_reserve(ImVec2(c.w, h));
        dl->AddRectFilled(p0, ImVec2(p0.x + c.w, p0.y + h), rv_editor_col(theme.code_base));
        rv_editor_catalog_frame(dl, ImVec2(p0.x + view.x, p0.y + view.y), view.scale, c.stale, theme);
        ImGui::EndGroup();
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

void rv_editor_catalog_cells(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    // Catalog cells of each kind, one whose picture failed, and a drop pocket to drop them on.
    const char *files[] = { "sprite.png", "tone.pcm", "main.lua", "main.scene.toml", "broken.png (no picture)" };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float cell = ImGui::GetFontSize() * 6.0f;
    for (int i = 0; i < 5; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cell", ImVec2(cell, cell * 0.7f));
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("RV_ASSET", files[i], std::strlen(files[i]) + 1);
            ImGui::TextUnformatted(files[i]);
            ImGui::EndDragDropSource();
        }
        char letter[2] = { 'F', '\0' };
        uint32_t color = 0;
        rv_editor_file_chip(files[i], letter[0], color);
        dl->AddText(ImVec2(p0.x + cell * 0.4f, p0.y + 4.0f), rv_editor_col(i == 4 ? theme.text_disabled : color), letter);
        dl->AddText(ImVec2(p0.x + 2.0f, p0.y + cell * 0.45f), rv_editor_col(i == 4 ? theme.warning : theme.text),
            files[i]);
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    rv_editor_text_field("##pocket", d.pocket, sizeof(d.pocket), theme);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("RV_ASSET")) {
            std::snprintf(d.pocket, sizeof(d.pocket), "%s", static_cast<const char *>(p->Data));
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Drop pocket: drag a cell here");
}

void rv_editor_catalog_type(const rv_editor_theme &theme)
{
    rv_editor_catalog_more_data &d = rv_editor_catalog_more;
    ImGui::Text("UI font %.0f px at this scale; the code text has its own size (View > Code Text Size).",
        ImGui::GetFontSize());
    ImGui::TextUnformatted("Interface: pdklib's 5x7 font in an 8 px line, 0O 1lI");
    rv_editor_font_code_push();
    ImGui::TextUnformatted("Code and logs: PxPlus IBM VGA 9x16, 0O 1lI");
    rv_editor_font_code_pop();
    // Editable next to read-only: the two must not be mistaken.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    rv_editor_text_field("##editable", d.editable, sizeof(d.editable), theme);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    rv_editor_text_field("##locked", d.locked, sizeof(d.locked), theme, { {}, true });
    // The viewer's thumbwheel.
    bool reset = false;
    d.wheel += rv_editor_thumbwheel("##cat_wheel", "Rot Y", false, ImGui::GetFontSize() * 14.0f, theme, reset);
    if (reset) {
        d.wheel = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Text("Rot Y %+.0f (double click resets)", std::fmod(d.wheel * 0.4f, 360.0f));
}

} // namespace rv_editor
