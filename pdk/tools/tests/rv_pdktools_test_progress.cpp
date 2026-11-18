// The progress protocol line by line: what is printed, what is read back, what is not a protocol line.

#include <cstdio>
#include <string>

#include "pdklib/rv_progress/rv_progress.hpp"
#include "rv_pdktools_test.hpp"

using namespace rv_pdklib;

namespace
{

// Runs `print` against a temporary stream and returns everything it wrote.
template <class F>
std::string capture(F print)
{
    std::FILE *f = std::tmpfile();
    print(f);
    std::fflush(f);
    std::rewind(f);
    std::string text;
    char buf[256];
    std::size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) {
        text.append(buf, got);
    }
    std::fclose(f);
    return text;
}

std::string without_newline(std::string text)
{
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

bool parses(const std::string &line, rv_progress_line &out)
{
    return rv_progress_parse_line(line, out);
}

} // namespace

RV_PDKTOOLS_TEST(progress_plan_prints_tool_tag_and_labels)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_plan("mppcburner", { "sources", "manifest", "burn" }, f);
    });
    RV_PDKTOOLS_CHECK(text == "mppcburner: [plan] sources manifest burn\n");
}

RV_PDKTOOLS_TEST(progress_plan_round_trip)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_plan("mppcinspector", { "disc", "scenes" }, f);
    });
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses(without_newline(text), line));
    RV_PDKTOOLS_CHECK(line.kind == rv_progress_kind::plan);
    RV_PDKTOOLS_CHECK(line.tool == "mppcinspector");
    RV_PDKTOOLS_CHECK(line.labels.size() == 2);
    RV_PDKTOOLS_CHECK(line.labels.size() == 2 && line.labels[0] == "disc" && line.labels[1] == "scenes");
}

RV_PDKTOOLS_TEST(progress_step_prints_tool_number_total_label)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_step("mppcburner", 1, 5, "sources", "./squid-maid", f);
    });
    RV_PDKTOOLS_CHECK(text.rfind("mppcburner: [1/5] sources", 0) == 0);
    RV_PDKTOOLS_CHECK(text.find("./squid-maid") != std::string::npos);
    RV_PDKTOOLS_CHECK(!text.empty() && text.back() == '\n');
}

RV_PDKTOOLS_TEST(progress_step_round_trip_with_spaces_in_detail)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_step("mppcmodbaker", 2, 3, "bake", "my models/a b.glb  (3 file(s))", f);
    });
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses(without_newline(text), line));
    RV_PDKTOOLS_CHECK(line.kind == rv_progress_kind::step);
    RV_PDKTOOLS_CHECK(line.tool == "mppcmodbaker");
    RV_PDKTOOLS_CHECK(line.n == 2 && line.total == 3);
    RV_PDKTOOLS_CHECK(line.label == "bake");
    RV_PDKTOOLS_CHECK(line.detail == "my models/a b.glb  (3 file(s))");
}

RV_PDKTOOLS_TEST(progress_step_round_trip_with_empty_detail)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_step("mppctexbaker", 3, 3, "write", "", f);
    });
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses(without_newline(text), line));
    RV_PDKTOOLS_CHECK(line.kind == rv_progress_kind::step);
    RV_PDKTOOLS_CHECK(line.n == 3 && line.total == 3);
    RV_PDKTOOLS_CHECK(line.label == "write");
    RV_PDKTOOLS_CHECK(line.detail.empty());
}

RV_PDKTOOLS_TEST(progress_end_ok_prints_and_round_trips)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_end_ok("mppcmusbaker", f);
    });
    RV_PDKTOOLS_CHECK(text == "mppcmusbaker: [end] ok\n");
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses(without_newline(text), line));
    RV_PDKTOOLS_CHECK(line.kind == rv_progress_kind::end_ok);
    RV_PDKTOOLS_CHECK(line.tool == "mppcmusbaker");
}

RV_PDKTOOLS_TEST(progress_end_failed_prints_and_round_trips)
{
    std::string text = capture([](std::FILE *f) {
        rv_progress_end_failed("mppcinspector", "2 error(s) in scenes", f);
    });
    RV_PDKTOOLS_CHECK(text == "mppcinspector: [end] failed: 2 error(s) in scenes\n");
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses(without_newline(text), line));
    RV_PDKTOOLS_CHECK(line.kind == rv_progress_kind::end_failed);
    RV_PDKTOOLS_CHECK(line.tool == "mppcinspector");
    RV_PDKTOOLS_CHECK(line.reason == "2 error(s) in scenes");
}

RV_PDKTOOLS_TEST(progress_parse_reads_documented_lines)
{
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses("mppcburner: [plan] sources manifest compile assets burn", line));
    RV_PDKTOOLS_CHECK(line.labels.size() == 5);
    RV_PDKTOOLS_CHECK(parses("mppcburner: [1/5] sources ./squid-maid", line));
    RV_PDKTOOLS_CHECK(line.n == 1 && line.total == 5 && line.label == "sources" && line.detail == "./squid-maid");
    RV_PDKTOOLS_CHECK(parses("mppcburner: [end] failed at sources", line) == false);
    RV_PDKTOOLS_CHECK(parses("mppcburner: [end] failed: at sources", line));
    RV_PDKTOOLS_CHECK(line.reason == "at sources");
}

RV_PDKTOOLS_TEST(progress_parse_rejects_foreign_lines)
{
    rv_progress_line line;
    // A real line must parse, or a parser that rejects everything would pass this test.
    RV_PDKTOOLS_CHECK(parses("mppcburner: [1/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("", line));
    RV_PDKTOOLS_CHECK(!parses("[1/4] manifest", line));
    RV_PDKTOOLS_CHECK(!parses("[plan] a b c", line));
    RV_PDKTOOLS_CHECK(!parses("[end] ok", line));
    RV_PDKTOOLS_CHECK(!parses("main.cpp:12:5: error: expected ';' after expression", line));
    RV_PDKTOOLS_CHECK(!parses("[12/40] Building CXX object CMakeFiles/a.dir/b.cpp.o", line));
    RV_PDKTOOLS_CHECK(!parses("scenes/menu.lua:3: error: unknown node 'x'", line));
    RV_PDKTOOLS_CHECK(!parses("mppcinspector: 2 error(s), 1 warning(s)", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: plan sources burn", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [end] maybe", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [bogus] x", line));
}

RV_PDKTOOLS_TEST(progress_parse_rejects_bad_numbers)
{
    rv_progress_line line;
    // A real line must parse, or a parser that rejects everything would pass this test.
    RV_PDKTOOLS_CHECK(parses("mppcburner: [1/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [0/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [6/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [a/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [1/b] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [-1/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [1/0] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [/5] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [1/] sources x", line));
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [1] sources x", line));
}

RV_PDKTOOLS_TEST(progress_parse_leaves_result_untouched_on_rejection)
{
    rv_progress_line line;
    // A real line must parse, or a parser that rejects everything would pass this test.
    RV_PDKTOOLS_CHECK(parses("mppcburner: [1/5] sources x", line));
    line.tool = "keep";
    RV_PDKTOOLS_CHECK(!parses("mppcburner: [0/5] sources x", line));
    RV_PDKTOOLS_CHECK(line.tool == "keep");
}

RV_PDKTOOLS_TEST(progress_parse_accepts_padded_label_of_old_ladder)
{
    // The old burner ladder pads the label to 11 columns; the padding is not part of the label.
    rv_progress_line line;
    RV_PDKTOOLS_CHECK(parses("mppcburner: [2/5] manifest    disc.toml", line));
    RV_PDKTOOLS_CHECK(line.label == "manifest");
    RV_PDKTOOLS_CHECK(line.detail == "disc.toml");
}
