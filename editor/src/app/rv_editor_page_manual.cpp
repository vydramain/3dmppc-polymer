// User manual page: embedded markdown rendered with ImGui.

#include "app/rv_editor_shell.hpp"

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

} // anonymous namespace

rv_editor_size rv_editor_manual_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_manual_content_width(), 0.0f);
}

void rv_editor_page_manual(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    (void)shell;

    const auto &blocks = rv_editor_manual_blocks();

    (void)rv_editor_tiled_area_begin("##manual_area", rv_editor_manual_content_width(), theme);
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
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
