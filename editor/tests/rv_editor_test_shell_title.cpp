// The Inspector tab (the observe pane) is titled "Inspector", whatever the layout preset.

#include <filesystem>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "pdk/rv_err.h"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_inspector_tab_title)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load(std::filesystem::path{}, error) == RV_OK);

    rv_editor_shell shell;
    const rv_editor_pane_id pane = 1;
    rv_editor_shell_title(shell, pane, rv_editor_pane_kind::observe);

    const auto it = shell.ws.titles.find(pane);
    RV_EDITOR_CHECK(it != shell.ws.titles.end() && it->second == "Inspector");
}
