// Release Controls: the status line under the buttons (candidate, verdict, checks, the first check not passed and how many
// more), the sha256 hover.

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
    // Five checks are open: the first by name, the other four as a count.
    RV_EDITOR_CHECK(
        facts.first_line == std::string("Candidate #7 | Not ready | 3/8 checks | Not passed: ") + c.checks[3].name + " +4");
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "abcdef"));
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "sha256"));
    RV_EDITOR_CHECK(facts.first_line.find('\n') == std::string::npos);
}

RV_EDITOR_TEST(rv_editor_test_release_facts_ready_candidate)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const rv_editor_candidate c = rv_editor_test_candidate(8);
    const auto facts = rv_editor_release_facts_make(&c, rv_editor_release_building::none);
    RV_EDITOR_CHECK(facts.first_line == "Candidate #7 | Ready | 8/8 checks");
}

RV_EDITOR_TEST(rv_editor_test_release_facts_not_passed_names_the_first_open_check_and_counts_the_rest)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    // 2 of 8 passed: "Not passed: <third check> +5".
    const rv_editor_candidate two = rv_editor_test_candidate(2);
    RV_EDITOR_CHECK(rv_editor_release_facts_make(&two, rv_editor_release_building::none).first_line ==
        std::string("Candidate #7 | Not ready | 2/8 checks | Not passed: ") + two.checks[2].name + " +5");
    // One open check: its name, no "+0".
    const rv_editor_candidate seven = rv_editor_test_candidate(7);
    RV_EDITOR_CHECK(rv_editor_release_facts_make(&seven, rv_editor_release_building::none).first_line ==
        std::string("Candidate #7 | Not ready | 7/8 checks | Not passed: ") + seven.checks[7].name);
    // A Passed result for other bytes does not count: the check is open again.
    rv_editor_candidate stale = rv_editor_test_candidate(8);
    stale.checks[0].hash = "another image";
    const std::string line = rv_editor_release_facts_make(&stale, rv_editor_release_building::none).first_line;
    RV_EDITOR_CHECK(rv_editor_test_has(line, "Not ready"));
    RV_EDITOR_CHECK(rv_editor_test_has(line, "7/8 checks"));
    RV_EDITOR_CHECK(rv_editor_test_has(line, (std::string("Not passed: ") + stale.checks[0].name).c_str()));
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
    RV_EDITOR_CHECK(plain.find("Not passed: ") != std::string::npos);
    RV_EDITOR_CHECK(candidate.first_line == "Building candidate | " + plain);
    RV_EDITOR_CHECK(development.first_line == "Building a development build | " + plain);
}

RV_EDITOR_TEST(rv_editor_test_release_facts_without_candidate_says_so_in_one_line)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const auto facts = rv_editor_release_facts_make(nullptr, rv_editor_release_building::none);
    RV_EDITOR_CHECK(!facts.first_line.empty());
    RV_EDITOR_CHECK(!rv_editor_test_has(facts.first_line, "#"));
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
