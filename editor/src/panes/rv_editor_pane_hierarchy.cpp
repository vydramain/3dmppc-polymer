// Hierarchy: the scene's objects as a tree. Selection is by id and shared with the
// viewport and Inspector; dragging one onto another moves it there keeping where
// it is in the scene, or says why it cannot. Dragging is only in the file's order.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_scene_actions.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

constexpr const char *rv_editor_scene_payload = "RV_SCENE_OBJECT";

// The frame's tree, by id: a context-menu Delete shifts the objects mid-frame, so indices would go stale.
// Ids whose object is gone are skipped when drawn.
struct rv_editor_hierarchy_tree {
    std::vector<std::string> roots;
    std::map<std::string, std::vector<std::string>> children;
};

// Asks the app to move the object under parent (empty: the root); the app's frame runs it.
void rv_editor_hierarchy_move(rv_editor_app &app, const std::string &id, const std::string &parent, bool keep_world)
{
    const rv_editor_pane_request request = {
        .kind = rv_editor_pane_request_kind::scene_move,
        .name = id,
        .parent = parent,
        .keep_world = keep_world,
    };
    (void)rv_editor_app_request(app, request);
}

// The last move's refusal, with a Dismiss button that asks the app to clear it.
void rv_editor_hierarchy_note(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (app.scene_move_note.empty()) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.warning));
    ImGui::TextWrapped("%s", app.scene_move_note.c_str());
    ImGui::PopStyleColor();
    const bool dismiss_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::dismiss_scene_note);
    const char *dismiss_why = dismiss_pending ? rv_editor_text("app_why_not.request_pending") : nullptr;
    const bool dismiss_clicked =
        rv_editor_button(rv_editor_text("findings.dismiss"), theme, { rv_editor_look::live, dismiss_why });
    if (dismiss_clicked) {
        (void)rv_editor_app_request(app, { .kind = rv_editor_pane_request_kind::dismiss_scene_note });
    }
}

// Lower case copy. Only ASCII letters change, so the comparison ignores case for ASCII only.
std::string rv_editor_hierarchy_lower(const std::string &text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Order of two objects under a sort other than the file's.
bool rv_editor_hierarchy_less(const rv_editor_scene_object &a, const rv_editor_scene_object &b, rv_editor_hierarchy_sort sort)
{
    if (sort == rv_editor_hierarchy_sort::name) {
        return rv_editor_hierarchy_lower(a.name) < rv_editor_hierarchy_lower(b.name);
    }
    return a.entity != b.entity ? a.entity < b.entity : a.kind < b.kind;
}

// The indices in the order the pane shows them: the file's order, or stably sorted.
std::vector<size_t> rv_editor_hierarchy_order(const rv_editor_scene_doc &doc, std::vector<size_t> indices)
{
    if (doc.hierarchy_sort == rv_editor_hierarchy_sort::file_order) {
        return indices;
    }
    std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return rv_editor_hierarchy_less(doc.scene.objects[a], doc.scene.objects[b], doc.hierarchy_sort);
    });
    return indices;
}

// Built once per frame. Sorting all objects first keeps each sibling group in the chosen order.
// An object whose parent is missing is a root, so the tree does not hide it.
rv_editor_hierarchy_tree rv_editor_hierarchy_build(const rv_editor_scene_doc &doc)
{
    std::vector<size_t> all;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        all.push_back(i);
    }
    std::unordered_set<std::string> ids;
    for (const rv_editor_scene_object &o : doc.scene.objects) {
        ids.insert(o.id);
    }
    rv_editor_hierarchy_tree tree;
    for (const size_t i : rv_editor_hierarchy_order(doc, all)) {
        const rv_editor_scene_object &o = doc.scene.objects[i];
        if (o.parent.empty() || ids.count(o.parent) == 0) {
            tree.roots.push_back(o.id);
        } else {
            tree.children[o.parent].push_back(o.id);
        }
    }
    return tree;
}

// The row's name: the name, kind and entity type, behind the eye mark when hidden.
std::string rv_editor_hierarchy_name(const rv_editor_scene_object &o)
{
    const char *node_name = o.name.empty() ? rv_editor_text("pane_hierarchy.unnamed") : o.name.c_str();
    const char *kind_label_str = rv_editor_scene_kind_label(o.kind);
    std::string name = o.entity.empty() ?
        rv_editor_text_format("pane_hierarchy.node_label", std::make_format_args(node_name, kind_label_str)) :
        rv_editor_text_format("pane_hierarchy.node_label_entity", std::make_format_args(node_name, kind_label_str, o.entity));
    if (!rv_editor_scene_has_component(o, component_hidden)) {
        return name;
    }
    const char *mark = rv_editor_text("pane_hierarchy.hidden_mark");
    return rv_editor_text_format("pane_hierarchy.joined", std::make_format_args(mark, name));
}

// Dim when hidden or when the Scene's layers leave it out; a dim row still selects.
bool rv_editor_hierarchy_dim(const rv_editor_scene_doc &doc, const rv_editor_scene_object &o)
{
    return rv_editor_scene_has_component(o, component_hidden) ||
        rv_editor_scene_visibility(doc, o) == rv_editor_visibility::absent;
}

// The components by name, dim, on the same line as the row's name.
void rv_editor_hierarchy_marks(const rv_editor_scene_object &o, const rv_editor_theme &theme)
{
    std::string marks;
    for (const rv_editor_scene_component &c : o.components) {
        if (c.name == component_hidden) {
            continue;
        }
        marks += marks.empty() ? c.name : std::string(rv_editor_text("pane_hierarchy.components_sep")) + c.name;
    }
    if (marks.empty()) {
        return;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
    ImGui::TextUnformatted(marks.c_str());
    ImGui::PopStyleColor();
}

// Context menu for a scene object: add children, duplicate, delete, reparent
void rv_editor_hierarchy_node_context_menu(rv_editor_app &app,
    rv_editor_scene_doc &doc,
    const rv_editor_scene_object &o,
    bool read_only)
{
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }
    rv_editor_scene_select(doc, o.id);
    ImGui::BeginDisabled(read_only);
    for (std::string_view kind : rv_editor_hierarchy_kinds()) {
        const std::string kind_name(kind);
        const char *kind_label = rv_editor_scene_kind_label(kind_name);
        const std::string add_label = rv_editor_text_format("pane_hierarchy.add_under", std::make_format_args(kind_label));
        if (ImGui::MenuItem(add_label.c_str())) {
            rv_editor_scene_add(doc, kind.data(), o.id);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(rv_editor_text("pane_hierarchy.duplicate"), rv_editor_text("pane_hierarchy.shortcut_duplicate"))) {
        rv_editor_scene_duplicate(doc, o.id);
    }
    if (ImGui::MenuItem(rv_editor_text("pane_hierarchy.delete"), rv_editor_text("pane_hierarchy.shortcut_delete"))) {
        rv_editor_scene_delete(doc, o.id);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(rv_editor_text("pane_hierarchy.move_to_root"))) {
        rv_editor_hierarchy_move(app, o.id, "", true);
    }
    // The other meaning, asked for by name: the numbers stay, the place changes.
    if (ImGui::MenuItem(rv_editor_text("pane_hierarchy.move_to_root_keep_local"))) {
        rv_editor_hierarchy_move(app, o.id, "", false);
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// One tree row by id, then its children when open. An id whose object is gone is skipped.
void rv_editor_hierarchy_node(rv_editor_app &app,
    const std::string &id,
    bool read_only,
    const rv_editor_theme &theme,
    const rv_editor_hierarchy_tree &tree)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int index = rv_editor_scene_find(doc.scene, id);
    if (index < 0) {
        return;
    }
    const rv_editor_scene_object o = doc.scene.objects[static_cast<size_t>(index)];
    const auto found = tree.children.find(o.id);
    const std::vector<std::string> no_children;
    const std::vector<std::string> &children = found == tree.children.end() ? no_children : found->second;
    const std::string label = rv_editor_hierarchy_name(o);
    rv_editor_tree_style style;
    style.leaf = children.empty();
    style.selected = doc.selected == o.id;
    style.dim = rv_editor_hierarchy_dim(doc, o);
    style.default_open = true;
    const rv_editor_tree_result row = rv_editor_tree_row(o.id.c_str(), label.c_str(), theme, style);
    if (row.clicked) {
        rv_editor_scene_select(doc, o.id);
    }
    // Only the file's order can be dragged: a sorted list has no place to drop into.
    const bool drag = !read_only && doc.hierarchy_sort == rv_editor_hierarchy_sort::file_order;
    if (drag && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(rv_editor_scene_payload, o.id.c_str(), o.id.size() + 1);
        ImGui::Text("%s", rv_editor_text_format("pane_hierarchy.move_label", std::make_format_args(label)).c_str());
        ImGui::EndDragDropSource();
    }
    if (drag && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(rv_editor_scene_payload)) {
            rv_editor_hierarchy_move(app, static_cast<const char *>(p->Data), o.id, true);
        }
        ImGui::EndDragDropTarget();
    }
    rv_editor_hierarchy_node_context_menu(app, doc, o, read_only);
    // After the node's own interaction: SameLine reuses the node's line.
    rv_editor_hierarchy_marks(o, theme);
    if (row.open) {
        for (const std::string &child : children) {
            rv_editor_hierarchy_node(app, child, read_only, theme, tree);
        }
        rv_editor_hierarchy_pieces(app, o, theme);
        rv_editor_tree_pop();
    }
}

// One search match: a flat row, selected by click, dimmed when hidden or left out by the Scene's layers.
// An id whose object is gone is skipped.
void rv_editor_hierarchy_match_row(rv_editor_scene_doc &doc, const std::string &id, const rv_editor_theme &theme)
{
    const int index = rv_editor_scene_find(doc.scene, id);
    if (index < 0) {
        return;
    }
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(index)];
    const std::string label = rv_editor_hierarchy_name(o);
    const rv_editor_list_style style = {
        .selected = doc.selected == o.id,
        .dim = rv_editor_hierarchy_dim(doc, o),
    };
    const rv_editor_list_result row = rv_editor_list_row(o.id.c_str(), label.c_str(), theme, style);
    if (row.clicked) {
        rv_editor_scene_select(doc, o.id);
    }
    rv_editor_hierarchy_marks(o, theme);
}

// The tree's roots, in the chosen order, each with its children below it.
void rv_editor_hierarchy_roots(rv_editor_app &app,
    const rv_editor_hierarchy_tree &tree,
    bool read_only,
    const rv_editor_theme &theme)
{
    for (const std::string &id : tree.roots) {
        rv_editor_hierarchy_node(app, id, read_only, theme, tree);
        if (app.scene == nullptr) {
            return;
        }
    }
}

// The objects whose name holds the query (ASCII case ignored), as one flat list.
void rv_editor_hierarchy_matches(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_scene_doc &doc = *app.scene;
    const std::string query = rv_editor_hierarchy_lower(doc.hierarchy_query);
    std::vector<size_t> matches;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        if (rv_editor_hierarchy_lower(doc.scene.objects[i].name).find(query) != std::string::npos) {
            matches.push_back(i);
        }
    }
    if (matches.empty()) {
        ImGui::TextDisabled("%s", rv_editor_text("pane_hierarchy.search_none"));
    }
    for (const size_t index : rv_editor_hierarchy_order(doc, matches)) {
        rv_editor_hierarchy_match_row(doc, doc.scene.objects[index].id, theme);
    }
}

// The search field and the sort choice, above the tree.
void rv_editor_hierarchy_filter(rv_editor_scene_doc &doc, const rv_editor_theme &theme)
{
    rv_editor_text_field("##hierarchy_query", doc.hierarchy_query, sizeof(doc.hierarchy_query), theme);
    // The field has no hint of its own: the hint is drawn over the empty box, unless it is being typed in.
    if (doc.hierarchy_query[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 pos = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
            rv_editor_col(theme.text_disabled),
            rv_editor_text("pane_hierarchy.search_hint"));
    }
    const char *const sort_items[] = {
        rv_editor_text("pane_hierarchy.sort_file_order"),
        rv_editor_text("pane_hierarchy.sort_name"),
        rv_editor_text("pane_hierarchy.sort_kind"),
    };
    int sort_index = static_cast<int>(doc.hierarchy_sort);
    if (rv_editor_dropdown("##hierarchy_sort", &sort_index, sort_items, std::ssize(sort_items), theme)) {
        doc.hierarchy_sort = static_cast<rv_editor_hierarchy_sort>(sort_index);
    }
}

} // namespace

void rv_editor_scene_keys(rv_editor_app &app, const rv_editor_ui_port &ui)
{
    if (app.scene == nullptr || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput) {
        return;
    }
    rv_editor_scene_doc &doc = *app.scene;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        rv_editor_scene_undo(doc);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        rv_editor_scene_redo(doc);
    }
    rv_editor_scene_camera cam = ui.state.scene_camera;
    if (ImGui::IsKeyChordPressed(ImGuiKey_Delete)) {
        // The command refuses with its own reason when it cannot run.
        rv_editor_scene_action_run(app, cam, rv_editor_scene_action::remove);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
        rv_editor_scene_action_run(app, cam, rv_editor_scene_action::duplicate);
    }
    if (cam != ui.state.scene_camera) {
        ui.requests.push_back({ .kind = rv_editor_ui_request_kind::set_scene_camera, .camera = cam });
    }
}

void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (app.scene == nullptr) {
        ImGui::TextWrapped("%s", rv_editor_text("pane_hierarchy.no_scene_open"));
        rv_editor_scene_open_row(app, theme);
        return;
    }
    rv_editor_scene_keys(app, ui);
    rv_editor_scene_doc &doc = *app.scene;
    const bool read_only = !doc.scene.read_only.empty();
    rv_editor_hierarchy_shelf(app, theme, ui);
    rv_editor_hierarchy_target(app, theme, ui);

    rv_editor_hierarchy_filter(doc, theme);
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const rv_editor_hierarchy_tree tree = rv_editor_hierarchy_build(doc);
    const auto body = [&]() {
        rv_editor_hierarchy_note(app, theme);
        if (doc.hierarchy_query[0] != '\0') {
            rv_editor_hierarchy_matches(app, theme);
        } else {
            rv_editor_hierarchy_roots(app, tree, read_only, theme);
        }
        if (app.scene == nullptr) {
            return;
        }
        // The rest of the pane is the root: dropped here, an object leaves its parent.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(rest.x, std::max(rest.y, ImGui::GetFrameHeight())));
        const bool drag = !read_only && doc.hierarchy_sort == rv_editor_hierarchy_sort::file_order;
        if (drag && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(rv_editor_scene_payload)) {
                rv_editor_hierarchy_move(app, static_cast<const char *>(p->Data), "", true);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked()) {
            rv_editor_scene_select(doc, std::string());
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
