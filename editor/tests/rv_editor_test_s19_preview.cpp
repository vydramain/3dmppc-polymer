// S19, R19 (no function lost), AC14: New Project shows under its buttons, in the content area, the description of the
// chosen template and one line per file the project will hold; choosing another template changes the lines, and the area
// frame and the buttons stay where they are.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/rv_editor_resources.hpp"
#include "project/rv_editor_templates.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s19_check.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s19;

namespace
{

constexpr float preview_w = 2400.0f; // roomy: a description or a file name is one line
constexpr float preview_h = 3000.0f; // tall: all of a template's file lines are drawn, so the probe sees each one
constexpr float max_line_h = 50.0f;  // a text line is far lower than the area
constexpr int max_indent = 8;        // spaces that may lead a file line

// The editor finds its disc templates beside its executable, where the editor target copies them; the test binary does not,
// so the test copies the repository's templates there when they are missing (they are small).
std::vector<rv_editor_template> templates_in_place()
{
    const std::vector<rv_editor_template> templates = rv_editor_templates();
    for (const rv_editor_template &t : templates) {
        std::error_code ec;
        if (!t.dir.empty() && !fs::is_directory(t.dir, ec)) {
            const fs::path source = fs::path(__FILE__).parent_path().parent_path().parent_path() / "mppcdiscs" / t.id;
            fs::create_directories(t.dir.parent_path(), ec);
            fs::copy(source, t.dir, fs::copy_options::recursive, ec);
        }
    }
    return templates;
}

// The width of each text in the page's font, measured in a frame.
std::vector<float> widths_of(rv_editor_test_imgui &gui, const std::vector<std::string> &texts)
{
    std::vector<float> out;
    gui.frame([&] {
        for (const std::string &t : texts) {
            out.push_back(ImGui::CalcTextSize(t.c_str()).x);
        }
    });
    return out;
}

// A line of plain text is a layout item of its own size and an empty label (rv_editor_text_item): ImGui's own text is no
// item and the probe does not see it. A line in the content area is in its scroll.
bool has_line(const rv_editor_probe_record &r, float width)
{
    for (const auto &e : r.entries) {
        if (e.label.empty() && e.in_scroll && std::fabs((e.x1 - e.x0) - width) <= 1.0f) {
            return true;
        }
    }
    return false;
}

// The number of plain text lines in the content area (items with an empty label inside a scroll, but not the area's own
// end item, which is as tall as the area).
size_t text_lines(const rv_editor_probe_record &r)
{
    size_t n = 0;
    for (const auto &e : r.entries) {
        n += e.label.empty() && e.in_scroll && e.y1 - e.y0 < max_line_h ? 1 : 0;
    }
    return n;
}

// The line of `text`, with up to max_indent spaces in front of it, is in the record.
bool has_text(rv_editor_test_imgui &gui, const rv_editor_probe_record &r, const std::string &text)
{
    std::vector<std::string> variants;
    for (int k = 0; k <= max_indent; ++k) {
        variants.push_back(std::string(static_cast<size_t>(k), ' ') + text);
    }
    for (float w : widths_of(gui, variants)) {
        if (has_line(r, w)) {
            return true;
        }
    }
    return false;
}

// "" when the two entries are at the same rectangle, else a description.
std::string same_place(const rv_editor_probe_entry *a, const rv_editor_probe_entry *b, const char *what)
{
    if (a == nullptr || b == nullptr) {
        return std::string(what) + " is not drawn";
    }
    return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1 ? "" : std::string(what) + " moved";
}

// The form as the user filled it for the template at `index`.
void select_template(fixture &f, size_t index)
{
    f.shell.start = {};
    std::snprintf(f.shell.start.name, sizeof(f.shell.start.name), "%s", "Game");
    std::snprintf(f.shell.start.id, sizeof(f.shell.start.id), "%s", "game");
    std::snprintf(f.shell.start.dir, sizeof(f.shell.start.dir), "%s", f.where.c_str());
    f.shell.start.template_index = index;
}

rv_editor_new_project project_for(const fixture &f, size_t index)
{
    return { "Game", "game", f.where, index };
}

// The item of a button by its text, outside scroll areas.
bool has_button(const rv_editor_probe_record &r, const char *id)
{
    return item_named(r, rv_editor_text(id)) != nullptr;
}

} // namespace

// AC14: on New Project with a template chosen, the content area holds its description and one line per file the project
// will hold, and the Create and Reset buttons are under the form.
RV_EDITOR_TEST(rv_editor_test_s19_new_project_preview_shows_the_template_description_and_its_files)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const std::vector<rv_editor_template> templates = templates_in_place();
    RV_EDITOR_CHECK(templates.size() >= 2);
    if (templates.size() < 2) {
        return;
    }
    const size_t index = 1;
    fixture f(gui, view::new_project);
    select_template(f, index);
    const auto r = f.settled(preview_w, preview_h);

    RV_EDITOR_CHECK(content_area(r) != nullptr);
    RV_EDITOR_CHECK(has_button(r, "shell_start_new.create_project"));
    RV_EDITOR_CHECK(has_button(r, "shell_start_new.reset"));
    RV_EDITOR_CHECK(has_text(gui, r, templates[index].description));
    const std::vector<std::string> files = rv_editor_new_project_files(project_for(f, index), templates[index]);
    RV_EDITOR_CHECK(!files.empty());
    for (const std::string &file : files) {
        RV_EDITOR_CHECK(has_text(gui, r, file));
    }
}

// AC14: another template replaces the description and the file lines; the area frame and the buttons do not move.
RV_EDITOR_TEST(rv_editor_test_s19_new_project_preview_follows_the_template_and_keeps_its_frame)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const std::vector<rv_editor_template> templates = templates_in_place();
    RV_EDITOR_CHECK(templates.size() >= 2);
    if (templates.size() < 2) {
        return;
    }
    fixture f(gui, view::new_project);
    select_template(f, 0);
    const auto first = f.settled(preview_w, preview_h);
    select_template(f, 1);
    const auto second = f.settled(preview_w, preview_h);

    const std::vector<std::string> files0 = rv_editor_new_project_files(project_for(f, 0), templates[0]);
    const std::vector<std::string> files1 = rv_editor_new_project_files(project_for(f, 1), templates[1]);
    RV_EDITOR_CHECK(has_text(gui, second, templates[1].description));
    RV_EDITOR_CHECK(!has_text(gui, second, templates[0].description));
    for (const std::string &file : files1) {
        RV_EDITOR_CHECK(has_text(gui, second, file));
    }
    // The area holds the description and one line per file of the selected template, and no line of the first one is left.
    RV_EDITOR_CHECK(text_lines(first) == 1 + files0.size());
    RV_EDITOR_CHECK(text_lines(second) == 1 + files1.size());
    RV_EDITOR_CHECK(has_text(gui, first, templates[0].description));

    const rv_editor_probe_entry *area_before = content_area(first);
    const rv_editor_probe_entry *area_after = content_area(second);
    RV_EDITOR_CHECK(area_before != nullptr && area_after != nullptr);
    if (area_before != nullptr && area_after != nullptr) {
        const std::string bad = same_place(area_before, area_after, "content area");
        RV_EDITOR_CHECK(bad.empty());
    }
    for (const char *id : { "shell_start_new.create_project", "shell_start_new.reset" }) {
        const std::string bad = same_place(item_named(first, rv_editor_text(id)), item_named(second, rv_editor_text(id)), id);
        RV_EDITOR_CHECK(bad.empty());
    }
}
