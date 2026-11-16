// Release Controls: the first line beside the tiles, the sha256 hover and the tile size choice.

#include <string>

#include "panes/rv_editor_release_facts.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

namespace
{

bool rv_editor_test_has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

// A hashed candidate with `passed` of its checks Passed on its bytes.
rv_editor_candidate rv_editor_test_candidate(size_t passed)
{
    rv_editor_candidate c;
    c.number = 7;
    c.sha256 = "abcdef0123456789abcdef0123456789";
    c.checks = rv_editor_checks_make();
    for (size_t i = 0; i < passed && i < c.checks.size(); ++i) {
        c.checks[i].state = rv_editor_check_state::passed;
        c.checks[i].hash = c.sha256;
    }
    return c;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_release_facts_not_ready_candidate)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const rv_editor_candidate c = rv_editor_test_candidate(3);
    const auto facts = rv_editor_release_facts_make(&c, rv_editor_release_building::none);
    RV_EDITOR_CHECK(facts.first_line == "Candidate #7 | Not ready | 3/8 checks");
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "abcdef"));
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "sha256"));
    RV_EDITOR_CHECK(facts.second_line);
}

RV_EDITOR_TEST(rv_editor_test_release_facts_ready_candidate)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const rv_editor_candidate c = rv_editor_test_candidate(8);
    const auto facts = rv_editor_release_facts_make(&c, rv_editor_release_building::none);
    RV_EDITOR_CHECK(facts.first_line == "Candidate #7 | Ready | 8/8 checks");
}

RV_EDITOR_TEST(rv_editor_test_release_facts_unhashed_candidate_has_no_hashing_note)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_candidate c = rv_editor_test_candidate(0);
    c.sha256.clear();
    const auto facts = rv_editor_release_facts_make(&c, rv_editor_release_building::none);
    RV_EDITOR_CHECK(rv_editor_test_has(facts.first_line, "Candidate #7"));
    RV_EDITOR_CHECK(rv_editor_test_has(facts.first_line, "0/8 checks"));
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "hashing"));
}

RV_EDITOR_TEST(rv_editor_test_release_facts_building_prefix)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const rv_editor_candidate c = rv_editor_test_candidate(3);
    const std::string plain = rv_editor_release_facts_make(&c, rv_editor_release_building::none).first_line;
    const auto candidate = rv_editor_release_facts_make(&c, rv_editor_release_building::candidate);
    const auto development = rv_editor_release_facts_make(&c, rv_editor_release_building::development);
    RV_EDITOR_CHECK(candidate.first_line == "Building candidate | " + plain);
    RV_EDITOR_CHECK(development.first_line == "Building a development build | " + plain);
}

RV_EDITOR_TEST(rv_editor_test_release_facts_without_candidate_is_one_line)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const auto facts = rv_editor_release_facts_make(nullptr, rv_editor_release_building::none);
    RV_EDITOR_CHECK(!facts.first_line.empty());
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "#"));
    RV_EDITOR_CHECK(!facts.second_line);
}

RV_EDITOR_TEST(rv_editor_test_release_number_tooltip_has_sha256)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const rv_editor_candidate c = rv_editor_test_candidate(3);
    const std::string tip = rv_editor_release_number_tooltip(c);
    RV_EDITOR_CHECK(rv_editor_test_has(tip, "sha256"));
    RV_EDITOR_CHECK(rv_editor_test_has(tip, c.sha256.c_str()));
}

RV_EDITOR_TEST(rv_editor_test_release_number_tooltip_unhashed_says_hashing)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_candidate c = rv_editor_test_candidate(0);
    c.sha256.clear();
    const std::string tip = rv_editor_release_number_tooltip(c);
    RV_EDITOR_CHECK(tip != "sha256 ");
    RV_EDITOR_CHECK(rv_editor_test_has(tip, "hashing"));
}

RV_EDITOR_TEST(rv_editor_test_release_tile_size_large_when_row_fits)
{
    rv_editor_test::rv_editor_test_imgui gui;
    gui.frame([&] {
        const rv_editor_tile tiles[5] = { { "Build", "B" },
            { "Run", "R" },
            { "Player", "P" },
            { "Stop", "S" },
            { "Report", "X" } };
        const float facts = 120.0f;
        float need = facts + ImGui::GetStyle().ItemSpacing.x * 5.0f;
        for (const rv_editor_tile &t : tiles) {
            need += rv_editor_tile_width(t, rv_editor_tile_size::large);
        }
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, facts, need) == rv_editor_tile_size::large);
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, facts, need + 200.0f) == rv_editor_tile_size::large);
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, facts, need - 1.0f) != rv_editor_tile_size::large);
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, facts, 10.0f) == rv_editor_tile_size::small);
    });
}
