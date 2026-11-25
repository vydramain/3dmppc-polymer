// AC15: the About page displays the editor's name and the version compiled into this build.

#include <regex>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "pdklib/rv_version/rv_version.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "theme/rv_editor_theme.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

RV_EDITOR_TEST(rv_editor_test_about_shows_editor_name_and_build_version)
{
    rv_editor_test_imgui gui;
    std::string drawn;
    gui.frame([&] {
        ImGui::LogToBuffer();
        rv_editor_page_about(rv_editor_theme_olive);
        drawn = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });

    std::string version_pattern = rv_pdklib::rv_version_str;
    version_pattern.insert(version_pattern.find('.'), "\\");
    const std::regex version_token("(^|[^0-9.])" + version_pattern + "($|[^0-9.])");
    RV_EDITOR_CHECK(drawn.find("3dmppc Editor") != std::string::npos);
    RV_EDITOR_CHECK(std::regex_search(drawn, version_token));
}
