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
#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_scene_actions.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_text.hpp"

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

// The search field's least width, in font sizes.
constexpr float query_min_em = 10.0f;

// The Dismiss button of a move note.
const char *rv_editor_hierarchy_dismiss_label()
{
    return rv_editor_text("findings.dismiss");
}

const char *const *rv_editor_hierarchy_sort_items()
{
    static const char *items[3];
    items[0] = rv_editor_text("pane_hierarchy.sort_file_order");
    items[1] = rv_editor_text("pane_hierarchy.sort_name");
    items[2] = rv_editor_text("pane_hierarchy.sort_kind");
    return items;
}

// The sort choice as wide as its longest item: the text, the arrow button and the frame padding.
float rv_editor_hierarchy_sort_width()
{
    float widest = 0.0f;
    for (int i = 0; i < 3; ++i) {
        widest = std::max(widest, ImGui::CalcTextSize(rv_editor_hierarchy_sort_items()[i]).x);
    }
    return widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float rv_editor_hierarchy_form_width()
{
    return ImGui::GetFontSize() * query_min_em + ImGui::GetStyle().ItemSpacing.x + rv_editor_hierarchy_sort_width();
}

float rv_editor_hierarchy_content_width()
{
    return std::max({ rv_editor_scene_none_width(),
        rv_editor_message_strip_min_width({ { rv_editor_hierarchy_dismiss_label() } }),
        rv_editor_tiled_shelf_width(rv_editor_hierarchy_shelf_width()) + ImGui::GetStyle().WindowPadding.x,
        rv_editor_hierarchy_form_width() + ImGui::GetStyle().WindowPadding.x });
}

// What the strip under the tree says: the last move's refusal with a Dismiss button that asks the app to clear it; with no
// scene, that and the way to get one; else nothing.
rv_editor_message_say rv_editor_hierarchy_say(const rv_editor_app &app)
{
    if (app.scene == nullptr) {
        return rv_editor_scene_none_say(app, rv_editor_text("pane_hierarchy.no_scene_open"), rv_editor_label_kind::dim);
    }
    if (app.scene_move_note.empty()) {
        return {};
    }
    return { app.scene_move_note, rv_editor_label_kind::warning, { rv_editor_hierarchy_dismiss_label() } };
}

// What the pressed button of that strip does.
void rv_editor_hierarchy_answer(rv_editor_app &app, const rv_editor_message_say &say, float width, const rv_editor_theme &theme)
{
    if (app.scene == nullptr) {
        rv_editor_scene_none_strip(app, say, width, theme);
        return;
    }
    const bool dismiss = rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, width) == 0;
    if (dismiss && !rv_editor_app_request_pending(app, rv_editor_pane_request_kind::dismiss_scene_note)) {
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
        const std::string name_a = rv_editor_hierarchy_lower(rv_editor_object_get_name(a));
        const std::string name_b = rv_editor_hierarchy_lower(rv_editor_object_get_name(b));
        return name_a < name_b;
    }
    return a.entity != b.entity ? a.entity < b.entity : rv_editor_object_get_kind(a) < rv_editor_object_get_kind(b);
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
    const std::string object_name = rv_editor_object_get_name(o);
    const char *node_name = object_name.empty() ? rv_editor_text("pane_hierarchy.unnamed") : object_name.c_str();
    const std::string object_kind = rv_editor_object_get_kind(o);
    const char *kind_label_str = rv_editor_scene_kind_label(object_kind);
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
        if (c.name == component_hidden || rv_editor_scene_component_is_data(c.name)) {
            continue;
        }
        marks += marks.empty() ? c.name : std::string(rv_editor_text("pane_hierarchy.components_sep")) + c.name;
    }
    if (marks.empty()) {
        return;
    }
    ImGui::SameLine();
    rv_editor_label(marks.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
}

// Context menu for a scene object: add children, duplicate, delete, reparent. Each item says why it cannot run.
void rv_editor_hierarchy_node_context_menu(rv_editor_app &app,
    const rv_editor_ui_port &ui,
    rv_editor_scene_doc &doc,
    const rv_editor_scene_object &o,
    bool read_only)
{
    if (!rv_editor_context_menu_begin(nullptr)) {
        return;
    }
    rv_editor_scene_select(doc, o.id);
    const char *read_only_why = read_only ? rv_editor_text("scene_actions.read_only") : nullptr;
    for (std::string_view kind : rv_editor_hierarchy_kinds()) {
        const std::string kind_name(kind);
        const char *kind_label = rv_editor_scene_kind_label(kind_name);
        const std::string add_label = rv_editor_text_format("pane_hierarchy.add_under", std::make_format_args(kind_label));
        if (rv_editor_menu_item(add_label.c_str(), nullptr, read_only_why)) {
            rv_editor_scene_add(doc, kind.data(), o.id);
        }
    }
    rv_editor_separator();
    const char *duplicate_why = rv_editor_scene_action_why_not(app, rv_editor_scene_action::duplicate);
    if (rv_editor_menu_item(rv_editor_text("pane_hierarchy.duplicate"),
            rv_editor_text("pane_hierarchy.shortcut_duplicate"),
            duplicate_why)) {
        rv_editor_hierarchy_run(app, ui, rv_editor_scene_action::duplicate);
    }
    const char *remove_why = rv_editor_scene_action_why_not(app, rv_editor_scene_action::remove);
    if (rv_editor_menu_item(rv_editor_text("pane_hierarchy.delete"),
            rv_editor_text("pane_hierarchy.shortcut_delete"),
            remove_why)) {
        rv_editor_hierarchy_run(app, ui, rv_editor_scene_action::remove);
    }
    rv_editor_separator();
    const char *detach_why = rv_editor_scene_action_why_not(app, rv_editor_scene_action::detach);
    if (rv_editor_menu_item(rv_editor_text("pane_hierarchy.move_to_root"), nullptr, detach_why)) {
        rv_editor_hierarchy_run(app, ui, rv_editor_scene_action::detach);
    }
    // The other meaning, asked for by name: the numbers stay, the place changes.
    if (rv_editor_menu_item(rv_editor_text("pane_hierarchy.move_to_root_keep_local"), nullptr, detach_why)) {
        rv_editor_hierarchy_move(app, o.id, "", false);
    }
    rv_editor_context_menu_end();
}

// One tree row by id, then its children when open. An id whose object is gone is skipped.
void rv_editor_hierarchy_node(rv_editor_app &app,
    const rv_editor_ui_port &ui,
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
        const std::string move_label = rv_editor_text_format("pane_hierarchy.move_label", std::make_format_args(label));
        rv_editor_label(move_label.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
        ImGui::EndDragDropSource();
    }
    if (drag && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(rv_editor_scene_payload)) {
            rv_editor_hierarchy_move(app, static_cast<const char *>(p->Data), o.id, true);
        }
        ImGui::EndDragDropTarget();
    }
    rv_editor_hierarchy_node_context_menu(app, ui, doc, o, read_only);
    // After the node's own interaction: SameLine reuses the node's line.
    rv_editor_hierarchy_marks(o, theme);
    if (row.open) {
        for (const std::string &child : children) {
            rv_editor_hierarchy_node(app, ui, child, read_only, theme, tree);
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
    const rv_editor_ui_port &ui,
    const rv_editor_hierarchy_tree &tree,
    bool read_only,
    const rv_editor_theme &theme)
{
    for (const std::string &id : tree.roots) {
        rv_editor_hierarchy_node(app, ui, id, read_only, theme, tree);
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
        if (rv_editor_hierarchy_lower(rv_editor_object_get_name(doc.scene.objects[i])).find(query) != std::string::npos) {
            matches.push_back(i);
        }
    }
    if (matches.empty()) {
        rv_editor_label(rv_editor_text("pane_hierarchy.search_none"), rv_editor_label_kind::dim, theme);
    }
    for (const size_t index : rv_editor_hierarchy_order(doc, matches)) {
        rv_editor_hierarchy_match_row(doc, doc.scene.objects[index].id, theme);
    }
}

// The form: the search field and the sort choice in a row above the tree, the field taking what the choice leaves. With
// no scene both are off.
void rv_editor_hierarchy_filter(rv_editor_app &app, const rv_editor_theme &theme)
{
    char none_query[1] = {};
    int none_sort = 0;
    ImGui::BeginDisabled(app.scene == nullptr);
    char *query = app.scene != nullptr ? app.scene->hierarchy_query : none_query;
    const size_t query_size = app.scene != nullptr ? sizeof(app.scene->hierarchy_query) : sizeof(none_query);
    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x - rv_editor_hierarchy_sort_width() - ImGui::GetStyle().ItemSpacing.x);
    rv_editor_text_field("##hierarchy_query", query, query_size, theme);
    // The field has no hint of its own: the hint is drawn over the empty box, unless it is being typed in.
    if (query[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 pos = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
            rv_editor_col(theme.text_disabled),
            rv_editor_text("pane_hierarchy.search_hint"));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(rv_editor_hierarchy_sort_width());
    int sort_index = app.scene != nullptr ? static_cast<int>(app.scene->hierarchy_sort) : none_sort;
    if (rv_editor_dropdown("##hierarchy_sort", &sort_index, rv_editor_hierarchy_sort_items(), 3, theme) &&
        app.scene != nullptr) {
        app.scene->hierarchy_sort = static_cast<rv_editor_hierarchy_sort>(sort_index);
    }
    ImGui::EndDisabled();
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

rv_editor_size rv_editor_hierarchy_minimum()
{
    // The shelf, the form (a frame and its spacing) and the tallest strip under the content area.
    const float form = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    return rv_editor_tiled_minimum(rv_editor_hierarchy_content_width(),
        rv_editor_tiled_shelf_block() + form + rv_editor_tiled_strip_block());
}

void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const float width = rv_editor_hierarchy_content_width();
    rv_editor_scene_keys(app, ui);
    rv_editor_hierarchy_shelf(app, theme, ui);
    rv_editor_hierarchy_filter(app, theme);
    const rv_editor_message_say say = rv_editor_hierarchy_say(app);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, width);
    const float strip_h = rv_editor_message_strip_height(say.text, say.buttons, strip_w);
    (void)rv_editor_tiled_area_begin("##tree_area", width, theme, {}, false, 0.0f, strip_h);
    if (app.scene == nullptr) {
        rv_editor_scene_files_list(app, theme);
        rv_editor_tiled_area_end(theme);
        rv_editor_hierarchy_answer(app, say, strip_w, theme);
        return;
    }
    rv_editor_scene_doc &doc = *app.scene;
    const bool read_only = !doc.scene.read_only.empty();
    const rv_editor_hierarchy_tree tree = rv_editor_hierarchy_build(doc);
    if (doc.hierarchy_query[0] != '\0') {
        rv_editor_hierarchy_matches(app, theme);
    } else {
        rv_editor_hierarchy_roots(app, ui, tree, read_only, theme);
    }
    if (app.scene != nullptr) {
        // The rest of the area is the root: dropped here, an object leaves its parent.
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
    }
    rv_editor_tiled_area_end(theme);
    rv_editor_hierarchy_answer(app, say, strip_w, theme);
}

} // namespace rv_editor
