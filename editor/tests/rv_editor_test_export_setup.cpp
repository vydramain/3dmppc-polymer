// The Export pane's installed marker for the Web support.

#include <filesystem>
#include <fstream>

#include "build/rv_editor_export_setup.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

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
