// The Export pane's visibility rules: the Web-support slot, the state line and the installed marker.

#include <filesystem>
#include <fstream>

#include "build/rv_editor_export_setup.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_export_setup_shown)
{
    for (const bool busy : { false, true }) {
        for (const bool installed : { false, true }) {
            RV_EDITOR_CHECK(rv_editor_export_setup_shown(rv_editor_export_kind::disc, installed, busy) ==
                rv_editor_export_setup_item::none);
            RV_EDITOR_CHECK(rv_editor_export_setup_shown(rv_editor_export_kind::player, installed, busy) ==
                rv_editor_export_setup_item::none);
        }
        RV_EDITOR_CHECK(
            rv_editor_export_setup_shown(rv_editor_export_kind::web, false, busy) == rv_editor_export_setup_item::button);
        RV_EDITOR_CHECK(rv_editor_export_setup_shown(rv_editor_export_kind::web, true, busy) ==
            rv_editor_export_setup_item::installed_note);
    }
}

RV_EDITOR_TEST(rv_editor_test_export_state_shown)
{
    RV_EDITOR_CHECK(!rv_editor_export_state_shown(rv_editor_build_state::idle));
    RV_EDITOR_CHECK(rv_editor_export_state_shown(rv_editor_build_state::building));
    RV_EDITOR_CHECK(rv_editor_export_state_shown(rv_editor_build_state::cancelling));
    RV_EDITOR_CHECK(rv_editor_export_state_shown(rv_editor_build_state::succeeded));
    RV_EDITOR_CHECK(rv_editor_export_state_shown(rv_editor_build_state::failed));
    RV_EDITOR_CHECK(rv_editor_export_state_shown(rv_editor_build_state::cancelled));
}

RV_EDITOR_TEST(rv_editor_test_web_support_installed_marker)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / "rv_editor_test_scratch_web_support";
    std::error_code ec;
    fs::remove_all(root, ec);
    // Missing directory and a directory without the marker: not installed.
    RV_EDITOR_CHECK(!rv_editor_web_support_installed(root));
    fs::create_directories(root, ec);
    RV_EDITOR_CHECK(!rv_editor_web_support_installed(root));
    // Another file is not the marker.
    std::ofstream(root / "other").put('x');
    RV_EDITOR_CHECK(!rv_editor_web_support_installed(root));
    std::ofstream(root / ".mppcburner-ready").put('x');
    RV_EDITOR_CHECK(rv_editor_web_support_installed(root));
    fs::remove_all(root, ec);
}
