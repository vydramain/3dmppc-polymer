// S19, R12/R13, AC13/AC14: New Project and Settings are forms (a label never crosses its field, a value whole, an error
// line and the buttons in the same place with and without an error) and their Browse is the big ask's card: the title, the
// directory list in the text area, the buttons whole at the minimum, and after the answer the form is back as it was.

#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_s19_check.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s19;

namespace
{

constexpr size_t new_valid = 1;
constexpr size_t new_bad_id = 2;
constexpr size_t new_create_error = 4;
constexpr size_t new_browse = 5;
constexpr size_t settings_loaded = 0;
constexpr size_t settings_problem = 2;
constexpr size_t settings_error = 4;
constexpr size_t settings_browse = 5;

// "" when the two entries are at the same rectangle, else a description.
std::string same_place(const rv_editor_probe_entry *a, const rv_editor_probe_entry *b, const char *what)
{
    if (a == nullptr || b == nullptr) {
        return std::string(what) + " is not drawn";
    }
    return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1 ? "" : std::string(what) + " moved";
}

// The text field of a row by the id its label gives it, in the S6 form ("##" + label) or the page's own.
const rv_editor_probe_entry *field_of(const rv_editor_probe_record &r, const char *label, const char *old_id)
{
    const rv_editor_probe_entry *e = item_with(r, std::string("##") + label);
    return e != nullptr ? e : item_with(r, old_id);
}

// The page at its minimum in state `i`.
rv_editor_probe_record at_min(fixture &f, minimum min, size_t i)
{
    f.states[i].set();
    return f.settled(min.w, min.h);
}

void expect_buttons_stay(rv_editor_test_imgui &gui, view v, std::vector<size_t> states, std::vector<const char *> buttons)
{
    fixture f(gui, v);
    const minimum min = minimum_of(gui, v);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const auto base = at_min(f, min, states[0]);
    for (const char *id : buttons) {
        RV_EDITOR_CHECK(item_named(base, rv_editor_text(id)) != nullptr);
    }
    for (size_t k = 1; k < states.size(); ++k) {
        const auto other = at_min(f, min, states[k]);
        for (const char *id : buttons) {
            const std::string bad = same_place(item_named(base, rv_editor_text(id)), item_named(other, rv_editor_text(id)), id);
            if (!bad.empty()) {
                std::fprintf(stderr, "  %s state %zu against %zu: %s\n", name_of(v), states[k], states[0], bad.c_str());
            }
            RV_EDITOR_CHECK(bad.empty());
        }
    }
}

// What a Browse card shows at the minimum of `v` in its browse state `i`, and how a Cancel gives the form back.
void expect_browse_card(rv_editor_test_imgui &gui, view v, size_t form_state, size_t browse, const char *action)
{
    fixture f(gui, v);
    const minimum min = minimum_of(gui, v);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    f.states[form_state].set();
    if (v == view::settings) {
        std::snprintf(f.shell.settings_paths[0], sizeof(f.shell.settings_paths[0]), "/usr/bin/tool");
    }
    const std::string typed = v == view::new_project ? f.shell.start.name : f.shell.settings_paths[0];
    RV_EDITOR_CHECK(!typed.empty());
    const auto form = f.settled(min.w, min.h);
    f.states[browse].set();
    const auto card = f.settled(min.w, min.h);
    const rv_editor_probe_entry *text = message_box(card);
    const rv_editor_probe_entry *ok = item_named(card, action);
    const rv_editor_probe_entry *cancel = item_named(card, rv_editor_text("browser.cancel"));
    if (text == nullptr || ok == nullptr || cancel == nullptr) {
        std::fprintf(stderr,
            "  %s browse: text area %s, '%s' %s, Cancel %s\n",
            name_of(v),
            text != nullptr ? "there" : "missing",
            action,
            ok != nullptr ? "there" : "missing",
            cancel != nullptr ? "there" : "missing");
    }
    RV_EDITOR_CHECK(text != nullptr && ok != nullptr && cancel != nullptr);
    if (text == nullptr || ok == nullptr || cancel == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(!s13::has_ask_place(card));
    // One row of buttons under the text area, the answer left of Cancel; the directory list is in the text area.
    RV_EDITOR_CHECK(ok->y0 == cancel->y0 && ok->x1 <= cancel->x0 && ok->y0 >= text->y1 - 0.5f);
    RV_EDITOR_CHECK(items_in_scroll(card) >= 3);
    RV_EDITOR_CHECK(item_named(form, action) == nullptr); // the form's items are gone while the card is up
    click(f, *cancel, min.w, min.h);
    const auto back = f.settled(min.w, min.h);
    RV_EDITOR_CHECK(message_box(back) == nullptr);
    RV_EDITOR_CHECK(moved(form, back).empty());
    RV_EDITOR_CHECK(typed == (v == view::new_project ? f.shell.start.name : f.shell.settings_paths[0]));
    RV_EDITOR_CHECK(v == view::new_project ? !f.shell.start.browsing : f.shell.settings_browse == -1);
}

} // namespace

// AC14: Create Project and Reset stay where they are (and are there) with a good form, a bad id and a failed create.
RV_EDITOR_TEST(rv_editor_test_s19_new_project_buttons_stay_with_and_without_an_error)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    expect_buttons_stay(gui,
        view::new_project,
        { new_valid, new_bad_id, new_create_error, 0 },
        { "shell_start_new.create_project", "shell_start_new.reset" });
}

// AC14: Apply and Revert stay where they are (and are there) with a loaded page, a tool problem and a save error.
RV_EDITOR_TEST(rv_editor_test_s19_settings_buttons_stay_with_and_without_an_error)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    expect_buttons_stay(gui,
        view::settings,
        { settings_loaded, settings_problem, settings_error },
        { "shell_settings.apply_button", "shell_settings.revert_button" });
}

// AC14: a value is whole in its field: with a 40 character name the Name field is as wide as the name, in a roomy tile.
RV_EDITOR_TEST(rv_editor_test_s19_new_project_field_shows_its_value_whole)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::new_project);
    f.states[new_valid].set();
    const std::string name = words(40);
    std::snprintf(f.shell.start.name, sizeof(f.shell.start.name), "%s", name.c_str());
    const auto r = f.settled(1600.0f, 700.0f);
    const rv_editor_probe_entry *field = field_of(r, rv_editor_text("shell_start_new.name"), "##name");
    RV_EDITOR_CHECK(field != nullptr);
    float need = 0.0f;
    gui.frame([&] {
        need = ImGui::CalcTextSize(name.c_str()).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    });
    if (field != nullptr && field->x1 - field->x0 < need) {
        std::fprintf(stderr, "  Name field %.1f wide, its value needs %.1f\n", field->x1 - field->x0, need);
    }
    RV_EDITOR_CHECK(field != nullptr && field->x1 - field->x0 >= need);
}

// AC14: the same with the Directory's value: a 150 character path is whole in a roomy tile and shows no "...".
RV_EDITOR_TEST(rv_editor_test_s19_new_project_directory_shows_its_path_whole)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::new_project);
    f.states[new_valid].set();
    std::string path = "/home/user";
    while (path.size() < 140) {
        path += "/deeply_nested_directory";
    }
    std::snprintf(f.shell.start.dir, sizeof(f.shell.start.dir), "%s", path.c_str());
    f.settled(2400.0f, 700.0f);
    const rv_editor_path_probe &probe = rv_editor_page_new_project_dir_probe();
    RV_EDITOR_CHECK(probe.shown == path);
    RV_EDITOR_CHECK(!contains(probe.shown, "..."));
}

// AC13: New Project's Browse is the card.
RV_EDITOR_TEST(rv_editor_test_s19_new_project_browse_is_a_card_and_the_form_comes_back)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    expect_browse_card(gui, view::new_project, new_valid, new_browse, "Select Directory");
}

// AC13: Settings' Browse is the card.
RV_EDITOR_TEST(rv_editor_test_s19_settings_browse_is_a_card_and_the_form_comes_back)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    expect_browse_card(gui, view::settings, settings_loaded, settings_browse, "Select");
}

// The card takes a body of the caller's: it is drawn in the text area in place of the text, and scrolls there.
RV_EDITOR_TEST(rv_editor_test_s19_mode_card_draws_the_callers_body_in_its_text_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const rv_editor_theme theme = stable_tiles::theme;
    int body_calls = 0;
    rv_editor_mode_card_spec spec;
    spec.title = "Select Directory";
    spec.text = "This text is not drawn when a body is given";
    spec.buttons = { "Select", "Cancel" };
    spec.body = [&] {
        ++body_calls;
        for (int i = 0; i < 30; ++i) {
            ImGui::Selectable(("entry " + std::to_string(i)).c_str());
        }
    };
    float w = 0.0f, h = 0.0f;
    gui.frame([&] {
        const ImVec2 m = rv_editor_mode_card_min_size(spec.buttons, theme);
        w = m.x + 2.0f * ImGui::GetStyle().WindowPadding.x;
        h = m.y + 2.0f * ImGui::GetStyle().WindowPadding.y;
    });
    rv_editor_probe_record r;
    for (int i = 0; i < 3; ++i) {
        r = rv_editor_probe_draw(
            gui,
            w,
            [&](rv_editor_probe_record &) {
                rv_editor_mode_card_begin("card", spec, theme);
                rv_editor_mode_card_end();
            },
            h);
    }
    RV_EDITOR_CHECK(body_calls > 0);
    RV_EDITOR_CHECK(message_box(r) != nullptr);
    RV_EDITOR_CHECK(items_in_scroll(r) >= 3);
    bool bar = false;
    for (const auto &e : r.entries) {
        bar = bar || (e.scroll_area && e.scrollbar);
    }
    RV_EDITOR_CHECK(bar); // 30 entries do not fit the minimum's two lines
}
