#include "ui/rv_editor_widgets_steps.hpp"

#include <algorithm>
#include <format>

#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr const char *separator = ">";

ImU32 rv_editor_step_colour(rv_editor_step_state state, const rv_editor_theme &theme)
{
    switch (state) {
    case rv_editor_step_state::done:
        return rv_editor_label_colour(rv_editor_label_kind::normal, theme);
    case rv_editor_step_state::running:
        return rv_editor_label_colour(rv_editor_label_kind::bright, theme);
    case rv_editor_step_state::pending:
        return rv_editor_label_colour(rv_editor_label_kind::dim, theme);
    case rv_editor_step_state::failed:
        return rv_editor_label_colour(rv_editor_label_kind::error, theme);
    case rv_editor_step_state::interrupted:
        break;
    }
    return rv_editor_label_colour(rv_editor_label_kind::warning, theme);
}

} // namespace

char rv_editor_step_mark(rv_editor_step_state state)
{
    switch (state) {
    case rv_editor_step_state::done:
        return 'v';
    case rv_editor_step_state::running:
        return '>';
    case rv_editor_step_state::pending:
        break;
    case rv_editor_step_state::failed:
        return 'x';
    case rv_editor_step_state::interrupted:
        return '!';
    }
    return ' ';
}

std::string rv_editor_steps_compact(const rv_editor_run &run)
{
    std::string result;
    const rv_editor_step *named = nullptr;
    for (const rv_editor_step &step : run.steps) {
        result += std::format("[{}]", rv_editor_step_mark(step.state));
        const bool active = step.state == rv_editor_step_state::running || step.state == rv_editor_step_state::failed ||
            step.state == rv_editor_step_state::interrupted;
        if (active && named == nullptr) {
            named = &step;
        }
    }
    if (named != nullptr) {
        result += " " + named->label;
    }
    return result;
}

std::string rv_editor_steps_compact(const rv_editor_progress &progress)
{
    return progress.runs.empty() ? std::string() : rv_editor_steps_compact(progress.runs.back());
}

rv_editor_steps_layout rv_editor_steps_layout_make(const rv_editor_run &run, float width)
{
    rv_editor_steps_layout layout;
    layout.permille = rv_editor_run_permille(run);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float gap = ImGui::CalcTextSize(separator).x + 2.0f * ImGui::GetStyle().ItemSpacing.x;
    const float line = ImGui::GetTextLineHeightWithSpacing();
    float x = 0.0f;
    int row = 0;
    for (const rv_editor_step &step : run.steps) {
        rv_editor_step_cell cell;
        cell.text = std::format("[{}] {}", rv_editor_step_mark(step.state), step.label);
        const float w = ImGui::CalcTextSize(cell.text.c_str()).x;
        if (x > 0.0f && x + w > width) {
            x = 0.0f;
            ++row;
        }
        cell.min = { origin.x + x, origin.y + row * line };
        cell.max = { cell.min.x + w, cell.min.y + ImGui::GetTextLineHeight() };
        x += w + gap;
        layout.cells.push_back(std::move(cell));
    }
    layout.rows = layout.cells.empty() ? 0 : row + 1;
    return layout;
}

rv_editor_steps_layout rv_editor_steps_draw(const rv_editor_run &run, const rv_editor_theme &theme)
{
    const rv_editor_steps_layout layout = rv_editor_steps_layout_make(run, ImGui::GetContentRegionAvail().x);
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < layout.cells.size(); ++i) {
        const rv_editor_step_cell &cell = layout.cells[i];
        draw->AddText(cell.min, rv_editor_step_colour(run.steps[i].state, theme), cell.text.c_str());
        // A separator follows a cell that a next one of the same row follows.
        if (i + 1 < layout.cells.size() && layout.cells[i + 1].min.y == cell.min.y) {
            const float x = cell.max.x + ImGui::GetStyle().ItemSpacing.x;
            draw->AddText({ x, cell.min.y }, rv_editor_label_colour(rv_editor_label_kind::dim, theme), separator);
        }
    }
    ImGui::Dummy({ 0.0f, std::max(1, layout.rows) * ImGui::GetTextLineHeightWithSpacing() });

    const int total = static_cast<int>(run.steps.size());
    const int done = static_cast<int>(std::count_if(run.steps.begin(), run.steps.end(), [](const rv_editor_step &s) {
        return s.state == rv_editor_step_state::done;
    }));
    const std::string count = std::format("{}/{}", done, total);
    ImGui::ProgressBar(static_cast<float>(layout.permille) / 1000.0f, { -1.0f, 0.0f }, count.c_str());
    for (const rv_editor_step &step : run.steps) {
        if (step.state == rv_editor_step_state::running && !step.detail.empty()) {
            rv_editor_label(step.detail.c_str(), rv_editor_label_kind::dim, theme);
            break;
        }
    }
    return layout;
}

} // namespace rv_editor
