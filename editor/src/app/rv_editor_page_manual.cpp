// User manual page: embedded markdown rendered with ImGui.

#include "app/rv_editor_shell.hpp"

#include <string_view>
#include <vector>

#include "imgui.h"

#include "rv_editor_manual_default.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

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

} // anonymous namespace

void rv_editor_page_manual(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    (void)shell;

    const auto &blocks = rv_editor_manual_blocks();

    rv_editor_pane_header(rv_editor_text("manual.title"), true, theme);
    ImGui::BeginChild("##manual", ImVec2(0.0f, 0.0f));
    for (const auto &block : blocks) {
        switch (block.kind) {
        case block_kind::heading: {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
            ImGui::TextUnformatted(block.text.c_str());
            ImGui::PopStyleColor();
            break;
        }
        case block_kind::subheading: {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_blue));
            ImGui::TextUnformatted(block.text.c_str());
            ImGui::PopStyleColor();
            break;
        }
        case block_kind::bullet: {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", block.text.c_str());
            break;
        }
        case block_kind::paragraph: {
            ImGui::TextWrapped("%s", block.text.c_str());
            break;
        }
        case block_kind::spacing: {
            ImGui::Spacing();
            break;
        }
        }
    }
    ImGui::EndChild();
}

} // namespace rv_editor
