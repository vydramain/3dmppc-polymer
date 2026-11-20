// mppcburner's step lists: the plan per target, the setup list, and that the numbers come from the list.

#include <cstdio>
#include <string>
#include <vector>

#include "pdklib/rv_progress/rv_progress.hpp"
#include "rv_burner_steps.hpp"
#include "rv_pdktools_test.hpp"

using namespace rv_pdklib;
using namespace rv_pdktools;

namespace
{

using labels_t = std::vector<std::string>;

// Prints a ladder for `labels` and returns the parsed protocol lines; a line that is not protocol is dropped.
std::vector<rv_progress_line> ladder_lines(const labels_t &labels)
{
    std::FILE *f = std::tmpfile();
    rv_burner_print_ladder(f, "mppcburner", labels);
    std::fflush(f);
    std::rewind(f);
    std::vector<rv_progress_line> lines;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f) != nullptr) {
        std::string text = buf;
        while (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }
        rv_progress_line line;
        if (rv_progress_parse_line(text, line)) {
            lines.push_back(line);
        }
    }
    std::fclose(f);
    return lines;
}

const labels_t k_build_steps = { "sources", "manifest", "compile", "assets", "burn" };

} // namespace

RV_PDKTOOLS_TEST(burner_build_plan_is_the_five_steps_for_the_disc_target)
{
    RV_PDKTOOLS_CHECK(rv_burner_build_steps("disc") == k_build_steps);
}

RV_PDKTOOLS_TEST(burner_build_plan_is_the_five_steps_for_the_player_target)
{
    RV_PDKTOOLS_CHECK(rv_burner_build_steps("player") == k_build_steps);
}

RV_PDKTOOLS_TEST(burner_build_plan_is_the_five_steps_for_the_web_target)
{
    RV_PDKTOOLS_CHECK(rv_burner_build_steps("web") == k_build_steps);
}

RV_PDKTOOLS_TEST(burner_setup_plan_is_the_seven_steps_in_order)
{
    const labels_t expected = { "check", "download", "verify", "unpack", "install", "embuilder", "mark" };
    RV_PDKTOOLS_CHECK(rv_burner_setup_steps() == expected);
}

RV_PDKTOOLS_TEST(burner_ladder_prints_the_plan_first_then_each_step_numbered_from_the_list)
{
    const labels_t labels = { "a", "b", "c" };
    const std::vector<rv_progress_line> lines = ladder_lines(labels);
    RV_PDKTOOLS_CHECK(lines.size() == 4);
    if (lines.size() != 4) {
        return;
    }
    RV_PDKTOOLS_CHECK(lines[0].kind == rv_progress_kind::plan && lines[0].tool == "mppcburner" && lines[0].labels == labels);
    for (int i = 1; i <= 3; ++i) {
        const rv_progress_line &step = lines[static_cast<std::size_t>(i)];
        RV_PDKTOOLS_CHECK(step.kind == rv_progress_kind::step && step.tool == "mppcburner");
        RV_PDKTOOLS_CHECK(step.n == i && step.total == 3 && step.label == labels[static_cast<std::size_t>(i - 1)]);
    }
}

RV_PDKTOOLS_TEST(burner_ladder_prints_no_end_line)
{
    for (const rv_progress_line &line : ladder_lines({ "a", "b" })) {
        RV_PDKTOOLS_CHECK(line.kind != rv_progress_kind::end_ok && line.kind != rv_progress_kind::end_failed);
    }
}

RV_PDKTOOLS_TEST(burner_ladder_with_an_added_step_shifts_the_numbers_and_the_plan)
{
    const std::vector<rv_progress_line> lines = ladder_lines({ "sources", "lint", "manifest", "burn" });
    RV_PDKTOOLS_CHECK(lines.size() == 5);
    if (lines.size() != 5) {
        return;
    }
    RV_PDKTOOLS_CHECK(lines[0].labels == labels_t({ "sources", "lint", "manifest", "burn" }));
    RV_PDKTOOLS_CHECK(lines[2].label == "lint" && lines[2].n == 2 && lines[2].total == 4);
    RV_PDKTOOLS_CHECK(lines[3].label == "manifest" && lines[3].n == 3 && lines[3].total == 4);
    RV_PDKTOOLS_CHECK(lines[4].label == "burn" && lines[4].n == 4 && lines[4].total == 4);
}
