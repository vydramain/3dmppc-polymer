// S5: Problems is the Inspector Report. Every starting layout keeps it as a tab of the log tile, a failed build opens it
// there, the layout file names it "inspector_report" (and still reads "problems"), and the texts no longer say Problems.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "pdk/rv_err.h"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

namespace
{

constexpr rv_editor_pane_kind report = rv_editor_pane_kind::inspector_report;

const rv_editor_layout_preset presets[] = { rv_editor_layout_preset::code,
    rv_editor_layout_preset::scene,
    rv_editor_layout_preset::debug,
    rv_editor_layout_preset::burn };

// The kind the log tile of each preset holds: Code the Output, Scene the Assets, Debug the Runtime Log, Burn the Build Log.
rv_editor_pane_kind log_kind(rv_editor_layout_preset p)
{
    switch (p) {
    case rv_editor_layout_preset::code:
        return rv_editor_pane_kind::output;
    case rv_editor_layout_preset::scene:
        return rv_editor_pane_kind::assets;
    case rv_editor_layout_preset::debug:
        return rv_editor_pane_kind::runtime_log;
    case rv_editor_layout_preset::burn:
        return rv_editor_pane_kind::build_log;
    }
    return rv_editor_pane_kind::empty;
}

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == RV_OK);
}

size_t count_kind(const rv_editor_pane_registry &panes, const rv_editor_layout &l, uint32_t leaf, rv_editor_pane_kind k)
{
    size_t n = 0;
    for (const rv_editor_pane_id id : l.nodes[leaf].leaf.tabs) {
        n += panes.panes[id].kind == k ? 1 : 0;
    }
    return n;
}

// The leaf holding a tab of `k`; rv_editor_tile_none when no leaf does.
uint32_t leaf_of(const rv_editor_pane_registry &panes, const rv_editor_layout &l, rv_editor_pane_kind k)
{
    for (uint32_t n = 0; n < l.nodes.size(); ++n) {
        if (l.nodes[n].kind == rv_editor_tile_kind::leaf && count_kind(panes, l, n, k) > 0) {
            return n;
        }
    }
    return rv_editor_tile_none;
}

size_t total_of(const rv_editor_pane_registry &panes, const rv_editor_layout &l, rv_editor_pane_kind k)
{
    size_t n = 0;
    for (uint32_t i = 0; i < l.nodes.size(); ++i) {
        n += l.nodes[i].kind == rv_editor_tile_kind::leaf ? count_kind(panes, l, i, k) : 0;
    }
    return n;
}

size_t leaf_count(const rv_editor_layout &l)
{
    size_t n = 0;
    for (const rv_editor_tile_node &node : l.nodes) {
        n += node.kind == rv_editor_tile_kind::leaf ? 1 : 0;
    }
    return n;
}

} // namespace

// AC4: every preset has one Inspector Report, a tab of the tile of its logs.
RV_EDITOR_TEST(rv_editor_test_inspector_report_in_every_preset)
{
    for (const rv_editor_layout_preset preset : presets) {
        rv_editor_pane_registry panes;
        rv_editor_layout layout;
        rv_editor_layout_preset_make(preset, panes, layout);
        RV_EDITOR_CHECK(total_of(panes, layout, report) == 1);
        const uint32_t leaf = leaf_of(panes, layout, report);
        RV_EDITOR_CHECK(leaf != rv_editor_tile_none);
        if (leaf == rv_editor_tile_none) {
            continue;
        }
        RV_EDITOR_CHECK(count_kind(panes, layout, leaf, log_kind(preset)) == 1);
        RV_EDITOR_CHECK(leaf_of(panes, layout, log_kind(preset)) == leaf);
    }
}

// AC4: the strips of controls hold one tab, never the report.
RV_EDITOR_TEST(rv_editor_test_inspector_report_strips_have_one_tab)
{
    for (const rv_editor_layout_preset preset : presets) {
        rv_editor_pane_registry panes;
        rv_editor_layout layout;
        rv_editor_layout_preset_make(preset, panes, layout);
        for (const rv_editor_pane_kind strip : { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls }) {
            const uint32_t leaf = leaf_of(panes, layout, strip);
            if (leaf == rv_editor_tile_none) {
                continue;
            }
            RV_EDITOR_CHECK(layout.nodes[leaf].leaf.tabs.size() == 1);
            RV_EDITOR_CHECK(count_kind(panes, layout, leaf, report) == 0);
        }
    }
}

// AC5: after a failed build in any preset the Inspector Report is the active tab of the log tile, which has the keyboard;
// no tile and no pane is added.
RV_EDITOR_TEST(rv_editor_test_inspector_report_failed_build_opens_it_in_the_log_tile)
{
    load_texts();
    for (const rv_editor_layout_preset preset : presets) {
        rv_editor_shell shell;
        rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
        shell.active = preset;
        const size_t leaves = leaf_count(shell.ws.layout);
        const size_t panes_before = shell.ws.panes.panes.size();
        rv_editor_app_build_alert_refused(shell.app, "no toolchain");
        RV_EDITOR_CHECK(shell.app.show_request == report);
        rv_editor_shell_focus_problems(shell);
        const uint32_t leaf = leaf_of(shell.ws.panes, shell.ws.layout, report);
        RV_EDITOR_CHECK(leaf != rv_editor_tile_none);
        if (leaf == rv_editor_tile_none) {
            continue;
        }
        const rv_editor_tile_leaf &l = shell.ws.layout.nodes[leaf].leaf;
        RV_EDITOR_CHECK(l.active < l.tabs.size() && shell.ws.panes.panes[l.tabs[l.active]].kind == report);
        RV_EDITOR_CHECK(count_kind(shell.ws.panes, shell.ws.layout, leaf, log_kind(preset)) == 1);
        RV_EDITOR_CHECK(shell.ws.focused_leaf == leaf);
        RV_EDITOR_CHECK(leaf_count(shell.ws.layout) == leaves);
        RV_EDITOR_CHECK(shell.ws.panes.panes.size() == panes_before);
        RV_EDITOR_CHECK(total_of(shell.ws.panes, shell.ws.layout, report) == 1);
    }
}

// AC5, the same when the tab is not in front of its neighbours: Debug with the Findings tab active.
RV_EDITOR_TEST(rv_editor_test_inspector_report_comes_in_front_of_other_log_tabs)
{
    load_texts();
    rv_editor_shell shell;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, shell.ws.panes, shell.ws.layout);
    shell.active = rv_editor_layout_preset::debug;
    const uint32_t leaf = leaf_of(shell.ws.panes, shell.ws.layout, rv_editor_pane_kind::findings);
    RV_EDITOR_CHECK(leaf != rv_editor_tile_none);
    if (leaf == rv_editor_tile_none) {
        return;
    }
    for (const rv_editor_pane_id id : shell.ws.layout.nodes[leaf].leaf.tabs) {
        if (shell.ws.panes.panes[id].kind == rv_editor_pane_kind::findings) {
            (void)rv_editor_tile_activate(shell.ws.layout, id);
        }
    }
    rv_editor_app_build_alert_refused(shell.app, "no toolchain");
    RV_EDITOR_CHECK(shell.app.show_request == report);
    rv_editor_shell_focus_problems(shell);
    const rv_editor_tile_leaf &l = shell.ws.layout.nodes[leaf].leaf;
    RV_EDITOR_CHECK(l.active < l.tabs.size() && shell.ws.panes.panes[l.tabs[l.active]].kind == report);
}

// AC23: the file names the pane "inspector_report"; a file of an earlier editor, naming it "problems", reads as the same kind.
RV_EDITOR_TEST(rv_editor_test_inspector_report_file_name)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::code, panes, layout);
    std::string text;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_layout_write(panes, layout, text, error) == RV_OK);
    RV_EDITOR_CHECK(text.find("inspector_report") != std::string::npos);
    RV_EDITOR_CHECK(text.find("problems") == std::string::npos);

    // The old spelling: every pane line naming the report says "problems".
    std::string old_text = text;
    for (size_t at = old_text.find("inspector_report"); at != std::string::npos; at = old_text.find("inspector_report", at)) {
        old_text.replace(at, std::strlen("inspector_report"), "problems");
    }
    RV_EDITOR_CHECK(old_text.find("inspector_report") == std::string::npos);
    rv_editor_pane_registry read_panes;
    rv_editor_layout read_layout;
    RV_EDITOR_CHECK(rv_editor_layout_read(old_text, read_panes, read_layout, error) == RV_OK);
    RV_EDITOR_CHECK(total_of(read_panes, read_layout, report) == 1);

    // Written again, it uses the new name only.
    std::string again;
    RV_EDITOR_CHECK(rv_editor_layout_write(read_panes, read_layout, again, error) == RV_OK);
    RV_EDITOR_CHECK(again.find("inspector_report") != std::string::npos);
    RV_EDITOR_CHECK(again.find("problems") == std::string::npos);
}

// AC23: a made-up name is still refused.
RV_EDITOR_TEST(rv_editor_test_inspector_report_file_refuses_unknown_name)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::code, panes, layout);
    std::string text;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_layout_write(panes, layout, text, error) == RV_OK);
    const size_t at = text.find("inspector_report");
    RV_EDITOR_CHECK(at != std::string::npos);
    if (at == std::string::npos) {
        return;
    }
    text.replace(at, std::strlen("inspector_report"), "inspector_reports");
    rv_editor_pane_registry p2;
    rv_editor_layout l2;
    RV_EDITOR_CHECK(rv_editor_layout_read(text, p2, l2, error) == RV_ERR_INVAL);
}

// AC4/AC23: the tab and the title say Inspector Report.
RV_EDITOR_TEST(rv_editor_test_inspector_report_texts)
{
    load_texts();
    rv_editor_shell shell;
    const rv_editor_pane_id pane = 1;
    shell.app.problems.resize(3);
    rv_editor_shell_title(shell, pane, report);
    const auto it = shell.ws.titles.find(pane);
    RV_EDITOR_CHECK(it != shell.ws.titles.end() && it->second == "Inspector Report (3)");

    // The tab label of the pane kind before a title is set.
    RV_EDITOR_CHECK(std::string(rv_editor_pane_title(report)) == "Inspector Report");
}

// AC23: the text file has no "Problems" left, the sample tab labels of the catalog included.
RV_EDITOR_TEST(rv_editor_test_inspector_report_texts_file_has_no_problems)
{
    const std::filesystem::path file =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "texts" / "rv_editor_texts.toml";
    std::ifstream in(file);
    RV_EDITOR_CHECK(in.good());
    std::stringstream all;
    all << in.rdbuf();
    const std::string text = all.str();
    RV_EDITOR_CHECK(!text.empty());
    RV_EDITOR_CHECK(text.find("Problems") == std::string::npos);
    RV_EDITOR_CHECK(text.find("Inspector Report ({})") != std::string::npos);
}
