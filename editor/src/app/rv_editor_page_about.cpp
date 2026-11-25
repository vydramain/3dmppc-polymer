#include "app/rv_editor_shell.hpp"

#include "imgui.h"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

void rv_editor_page_about(const rv_editor_theme &)
{
    ImGui::TextUnformatted("3dmppc Editor");
    ImGui::TextUnformatted(rv_pdklib::rv_version_str);
}

} // namespace rv_editor
