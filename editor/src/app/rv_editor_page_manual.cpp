// Editor walkthroughs and the embedded reference manual.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h" // an item added without taking room

#include "rv_editor_manual_default.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr std::string_view prefix_subheading = "## ";
constexpr std::string_view prefix_heading = "# ";
constexpr std::string_view prefix_bullet = "- ";

enum class block_kind {
    heading,    // prefix_heading
    subheading, // prefix_subheading
    bullet,     // prefix_bullet
    paragraph,  // text lines
    spacing     // blank line
};

struct block {
    block_kind kind;
    std::string text;
};

std::vector<block> rv_editor_manual_parse(std::string_view text)
{
    std::vector<block> blocks;
    size_t pos = 0;
    std::string para_text;
    auto flush = [&blocks, &para_text]() {
        if (!para_text.empty()) {
            blocks.push_back({ block_kind::paragraph, para_text });
            para_text.clear();
        }
    };

    while (pos < text.size()) {
        size_t line_end = text.find('\n', pos);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        const std::string_view line = text.substr(pos, line_end - pos);
        pos = line_end + (line_end < text.size() ? 1 : 0);

        if (line.empty()) {
            flush();
            blocks.push_back({ block_kind::spacing, "" });
        } else if (line.starts_with(prefix_subheading)) {
            flush();
            const std::string_view title = line.substr(prefix_subheading.size());
            blocks.push_back({ block_kind::subheading, std::string(title) });
        } else if (line.starts_with(prefix_heading)) {
            flush();
            const std::string_view title = line.substr(prefix_heading.size());
            blocks.push_back({ block_kind::heading, std::string(title) });
        } else if (line.starts_with(prefix_bullet)) {
            flush();
            const std::string_view item = line.substr(prefix_bullet.size());
            blocks.push_back({ block_kind::bullet, std::string(item) });
        } else {
            if (!para_text.empty()) {
                para_text += " ";
            }
            para_text += line;
        }
    }
    flush();
    return blocks;
}

const std::vector<block> &rv_editor_manual_blocks()
{
    static const std::vector<block> blocks = rv_editor_manual_parse(rv_editor_manual_default);
    return blocks;
}

// The content area is at least this wide, in font sizes.
constexpr float area_min_em = 20.0f;

float rv_editor_manual_content_width()
{
    return ImGui::GetFontSize() * area_min_em;
}

void rv_editor_manual_reference_draw(const rv_editor_theme &theme)
{
    const auto &blocks = rv_editor_manual_blocks();
    for (const auto &block : blocks) {
        // The text of a block is an item of the layout too (a rectangle with an id, nothing to click).
        const auto known = [&] {
            ImGui::ItemAdd(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()),
                ImGui::GetID(&block),
                nullptr,
                ImGuiItemFlags_NoNav);
        };
        switch (block.kind) {
        case block_kind::heading: {
            rv_editor_label(block.text.c_str(), rv_editor_label_kind::bright, theme);
            known();
            break;
        }
        case block_kind::subheading: {
            rv_editor_label(block.text.c_str(), rv_editor_label_kind::info, theme);
            known();
            break;
        }
        case block_kind::bullet: {
            rv_editor_label(block.text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::bullet);
            known();
            break;
        }
        case block_kind::paragraph: {
            rv_editor_label(block.text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
            known();
            break;
        }
        case block_kind::spacing: {
            ImGui::Spacing();
            break;
        }
        }
    }
}

bool rv_editor_manual_controls_visible(const rv_editor_workspace &ws)
{
    const auto &layout = ws.layout;
    for (uint32_t leaf = 0; leaf < layout.nodes.size(); ++leaf) {
        const auto &node = layout.nodes[leaf];
        const bool visible = layout.maximized_leaf == rv_editor_tile_none || layout.maximized_leaf == leaf;
        if (visible && node.kind == rv_editor_tile_kind::leaf && node.leaf.active < node.leaf.tabs.size() &&
            ws.panes.panes[node.leaf.tabs[node.leaf.active]].kind == rv_editor_pane_kind::controls) {
            return true;
        }
    }
    return false;
}

using manual_step = rv_editor_manual_step;

const char *rv_editor_manual_build_run_hint(const rv_editor_shell &shell)
{
    const auto &app = shell.app;
    if (app.build.busy()) {
        return "manual_guide.wait_build";
    }
    if (shell.manual.step == manual_step::run_project && app.session.state() == rv_editor_run_state::starting) {
        return "manual_guide.wait_run";
    }
    if (shell.active == rv_editor_layout_preset::burn) {
        return "manual_guide.select_code";
    }
    if (!rv_editor_manual_controls_visible(shell.ws)) {
        return shell.ws.layout.maximized_leaf == rv_editor_tile_none ? "manual_guide.show_controls" :
                                                                       "manual_guide.restore_tile";
    }
    if (shell.manual.step == manual_step::build_project) {
        return "manual_guide.build";
    }
    return app.session.live() ? "manual_guide.stop_session" : "manual_guide.run";
}

} // namespace

void rv_editor_shell_manual_start(rv_editor_shell &shell, rv_editor_manual_step first)
{
    shell.manual = {};
    shell.manual.step = first;
    shell.manual.build_first_seq = shell.app.build_first_seq;
    shell.manual.session = shell.app.session.number();
}

void rv_editor_shell_manual_update(rv_editor_shell &shell)
{
    auto &guide = shell.manual;
    const auto &app = shell.app;
    if (guide.step == manual_step::close_tile && !rv_editor_shell_pane_kept(shell, guide.pane)) {
        guide.step = manual_step::complete;
    }
    if (guide.step == manual_step::build_project && app.build_first_seq != guide.build_first_seq && app.build.image().empty() &&
        app.build.state() == rv_editor_build_state::succeeded) {
        guide.step = manual_step::run_project;
        guide.build_number = app.build.number();
    }
    const auto number = app.session.number();
    if (guide.step == manual_step::run_project && number != guide.session && number != app.candidate_session &&
        app.session.state() == rv_editor_run_state::running && app.session.build_number() >= guide.build_number) {
        guide.step = manual_step::complete;
    }
}

static const char *rv_editor_shell_manual_hint(const rv_editor_shell &shell)
{
    switch (shell.manual.step) {
    case manual_step::none:
        return nullptr;
    case manual_step::open_picker:
        return rv_editor_text("manual_guide.open_picker");
    case manual_step::pick_project:
        return rv_editor_text(shell.leaving == rv_editor_review_action::open && shell.review.error.empty() ?
                "manual_guide.finish_open" :
                "manual_guide.pick_project");
    case manual_step::new_tile:
        return rv_editor_text(
            shell.ws.layout.maximized_leaf == rv_editor_tile_none ? "manual_guide.new_tile" : "manual_guide.restore_tile");
    case manual_step::close_tile: {
        if (shell.active != shell.manual.tile_layout) {
            return rv_editor_text("manual_guide.return_to_tile");
        }
        const auto &layout = shell.ws.layout;
        const bool hidden = layout.maximized_leaf != rv_editor_tile_none &&
            layout.maximized_leaf != rv_editor_tile_find(layout, shell.manual.pane);
        return rv_editor_text(hidden ? "manual_guide.restore_tile" : "manual_guide.close_tile");
    }
    case manual_step::build_project:
    case manual_step::run_project:
        return rv_editor_text(rv_editor_manual_build_run_hint(shell));
    case manual_step::complete:
        return rv_editor_text("manual_guide.complete");
    }
    return nullptr;
}

float rv_editor_shell_manual_draw(rv_editor_shell &shell, const rv_editor_theme &theme, ImVec2 top, float width)
{
    const char *hint = rv_editor_shell_manual_hint(shell);
    if (hint == nullptr) {
        return 0.0f;
    }
    const char *end =
        rv_editor_text(shell.manual.step == rv_editor_manual_step::complete ? "manual_guide.dismiss" : "manual_guide.end");
    const auto &style = ImGui::GetStyle();
    const float text_width =
        std::max(1.0f, width - 2.0f * style.WindowPadding.x - style.ItemSpacing.x - rv_editor_button_width(end));
    const float text_height = ImGui::CalcTextSize(hint, nullptr, false, text_width).y;
    const float height =
        std::max(ImGui::GetFrameHeight(), text_height + 2.0f * style.FramePadding.y) + 2.0f * style.WindowPadding.y;
    ImGui::SetNextWindowPos(top);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##manual_guide", nullptr, flags)) {
        const float left = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(left + text_width);
        rv_editor_label(hint, rv_editor_label_kind::info, theme, rv_editor_label_layout::line);
        ImGui::PopTextWrapPos();
        ImGui::SameLine(left + text_width + style.ItemSpacing.x);
        if (rv_editor_button(end, theme)) {
            shell.manual = {};
        }
    }
    ImGui::End();
    return height;
}

rv_editor_size rv_editor_manual_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_manual_content_width(), 0.0f);
}

void rv_editor_page_manual(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    (void)rv_editor_tiled_area_begin("##manual_area", rv_editor_manual_content_width(), theme);
    if (shell.manual_reference) {
        if (rv_editor_button(rv_editor_text("manual_guide.back"), theme)) {
            shell.manual_reference = false;
        }
        rv_editor_manual_reference_draw(theme);
    } else {
        if (rv_editor_button(rv_editor_text("manual_guide.open"), theme)) {
            rv_editor_shell_manual_start(shell, manual_step::open_picker);
        }
        const char *why_not = shell.app.project.open ? nullptr : rv_editor_text("manual_guide.need_project");
        if (rv_editor_button(rv_editor_text("manual_guide.tiles"), theme, { rv_editor_look::live, why_not })) {
            rv_editor_shell_manual_start(shell, manual_step::new_tile);
        }
        if (rv_editor_button(rv_editor_text("manual_guide.build_run"), theme, { rv_editor_look::live, why_not })) {
            rv_editor_shell_manual_start(shell, manual_step::build_project);
        }
        if (rv_editor_button(rv_editor_text("manual_guide.reference"), theme)) {
            shell.manual_reference = true;
        }
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
