// Observe: what the running session says about itself, and its persistent Lua
// state read through `keys` and `get` (read-only). Session: what ran, and
// after it ends, how.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_observe.hpp"
#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Protocol field: value found flag from console answer.
constexpr std::string_view field_found = "found";
// Protocol field: Lua type descriptor from console answer.
constexpr std::string_view field_type = "type";
// Protocol field: value content from console answer.
constexpr std::string_view field_value = "value";
// Protocol field: key count in table response from console.
constexpr std::string_view field_count = "count";
// Protocol field: enumerated keys list from console answer.
constexpr std::string_view field_keys = "keys";
// Protocol field: visible key count in keys response from console.
constexpr std::string_view field_shown = "shown";
// Protocol value: true for boolean and found fields (see src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view protocol_value_true = "1";
// Protocol marker: string key type in keys list entry (see src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr char protocol_key_type_string = 's';
// Protocol marker: index key type in keys list entry (see src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr char protocol_key_type_index = 'i';

// Lua type string: boolean.
constexpr std::string_view type_boolean = "boolean";
// Lua type string: string.
constexpr std::string_view type_string = "string";
// Lua type string: table.
constexpr std::string_view type_table = "table";
// Lua type string: number.
constexpr std::string_view type_number = "number";

// Root prefix for Lua state paths.
constexpr std::string_view state_root = "state";
// Separator between path components.
constexpr std::string_view path_dot = ".";

// Separator in command and pin path construction.
constexpr std::string_view sep_space = " ";
// Characters splitting key paths.
constexpr std::string_view delim_whitespace = " \t\n";

// Pins table columns count.
constexpr int pins_table_cols = 4;
// Keys table columns count.
constexpr int keys_table_cols = 3;

// Value type kind.
enum class value_kind {
    boolean,
    string,
    table,
    number,
    unknown
};

// Map Lua type names to value kinds.
constexpr std::array<std::pair<std::string_view, value_kind>, 4> value_kind_map = { { { type_boolean, value_kind::boolean },
    { type_string, value_kind::string },
    { type_table, value_kind::table },
    { type_number, value_kind::number } } };

// Find value kind by type name.
value_kind find_value_kind(std::string_view type)
{
    for (const auto &[type_str, kind] : value_kind_map) {
        if (type_str == type) {
            return kind;
        }
    }
    return value_kind::unknown;
}

} // namespace

namespace
{

// Where the keys listed are: "state" and the keys down to the shown table.
std::string rv_editor_path_text(const std::vector<std::string> &path)
{
    std::string out{ state_root };
    for (const std::string &key : path) {
        out += path_dot;
        out += key;
    }
    return out;
}

// A key path as a request spells it: keys separated by spaces.
std::string rv_editor_path_name(const std::vector<std::string> &path)
{
    std::string out;
    bool first = true;
    for (const std::string &key : path) {
        if (!first) {
            out += sep_space;
        }
        out += key;
        first = false;
    }
    return out;
}

// Why an observe control is unavailable now: the app still runs an observe request.
const char *observe_busy_why(const rv_editor_app &app)
{
    return rv_editor_app_observe_waiting(app) ? rv_editor_text("app_why_not.request_pending") : nullptr;
}

// Leaves a request for the app to run; a refusal is logged by the app.
void rv_editor_observe_leave(rv_editor_app &app, rv_editor_pane_request_kind kind, const std::string &name)
{
    (void)rv_editor_app_request(app, { kind, rv_editor_tile_none, {}, name });
}

// What a `get` answer holds, as a person reads it.
std::string rv_editor_value_text(const rv_editor_answer &a)
{
    if (!a.ok) {
        const std::string &error = a.error;
        return rv_editor_text_format("pane_observe.value_unavailable", std::make_format_args(error));
    }
    const auto field = [&a](std::string_view key) {
        for (const auto &[k, v] : a.fields) {
            if (k == key) {
                return v;
            }
        }
        return std::string();
    };
    const std::string type = field(field_type);
    if (field(field_found) != protocol_value_true) {
        return rv_editor_text("pane_observe.value_absent");
    }
    switch (find_value_kind(type)) {
    case value_kind::boolean:
        return field(field_value) == protocol_value_true ? rv_editor_text("pane_observe.value_true") :
                                                           rv_editor_text("pane_observe.value_false");
    case value_kind::string:
        return "\"" + rv_editor_hex_decode(field(field_value)) + "\"";
    case value_kind::table: {
        const std::string &count = field(field_count);
        return rv_editor_text_format("pane_observe.value_table", std::make_format_args(count));
    }
    case value_kind::number:
        return field(field_value);
    case value_kind::unknown:
        return type;
    }
}

// When an answer was true: the frame it saw on a paused machine, otherwise the
// moment it was read from a running one; never a claim that answers share a frame.
std::string rv_editor_updated_text(const rv_editor_answer &a, const rv_editor_session &s)
{
    if (!a.frame_exact) {
        const std::string &clock_str = rv_editor_clock(a.at);
        return rv_editor_text_format("pane_observe.updated_sampled", std::make_format_args(clock_str));
    }
    const bool current = s.state() == rv_editor_run_state::paused && a.frame == s.frame();
    const auto frame_num = std::to_string(a.frame);
    const char *frame_key = current ? "pane_observe.updated_frame" : "pane_observe.updated_frame_stale";
    return rv_editor_text_format(frame_key, std::make_format_args(frame_num));
}

struct rv_editor_key_row {
    std::string name;  // as a request spells it; empty when a request cannot
    std::string shown; // as the list shows it
    std::string type;
};

// `keys` answers each entry as s<hex>:<type>, i<n>:<type> or x:<type>.
std::vector<rv_editor_key_row> rv_editor_key_rows(const rv_editor_answer &a)
{
    std::vector<rv_editor_key_row> rows;
    std::string list;
    for (const auto &[k, v] : a.fields) {
        if (k == field_keys) {
            list = v;
        }
    }
    size_t p = 0;
    while (p < list.size()) {
        const size_t comma = std::min(list.find(',', p), list.size());
        const std::string entry = list.substr(p, comma - p);
        p = comma + 1;
        const size_t colon = entry.rfind(':');
        if (entry.empty() || colon == std::string::npos) {
            continue;
        }
        rv_editor_key_row row;
        row.type = entry.substr(colon + 1);
        const std::string key = entry.substr(1, colon - 1);
        switch (entry[0]) {
        case protocol_key_type_string: {
            row.shown = rv_editor_hex_decode(key);
            // The channel splits a request at spaces: such a key cannot be asked for.
            const bool plain = !row.shown.empty() && row.shown.find_first_of(delim_whitespace) == std::string::npos;
            row.name = plain ? row.shown : std::string();
            break;
        }
        case protocol_key_type_index:
            row.shown = "[" + key + "]";
            row.name = key;
            break;
        default:
            row.shown = rv_editor_text("pane_observe.value_unknown_key");
        }
        rows.push_back(row);
    }
    return rows;
}

// The widest row: the shelf's two buttons or the box's button.
float rv_editor_observe_content_width()
{
    return std::max(rv_editor_message_strip_min_width({ { rv_editor_text("panes.open_project_box") } }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_observe.refresh"), rv_editor_text("pane_observe.up") }));
}

// Why Refresh and Up are grey with no machine to read; nullptr when there is one.
const char *rv_editor_observe_no_machine_why(const rv_editor_app &app)
{
    if (!app.project.open) {
        return rv_editor_text("panes.no_project_open_message");
    }
    if (!app.session.live()) {
        return rv_editor_text("pane_observe.no_session");
    }
    return app.session.connected() ? nullptr : rv_editor_text("pane_observe.waiting_console");
}

// Refresh and Up in one row, always there: grey with the reason when they cannot act.
void rv_editor_observe_shelf(rv_editor_app &app, bool lua, const rv_editor_theme &theme)
{
    const rv_editor_observe &o = app.observe;
    const char *machine_why = rv_editor_observe_no_machine_why(app);
    rv_editor_shelf_begin("##shelf", theme);
    const char *refresh_why = machine_why != nullptr ? machine_why : observe_busy_why(app);
    if (rv_editor_button(rv_editor_text("pane_observe.refresh"), theme, { rv_editor_look::live, refresh_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::observe_refresh });
    }
    ImGui::SameLine();
    const char *up_why = !lua ? (machine_why != nullptr ? machine_why : rv_editor_text("pane_observe.no_lua_machine")) :
        o.path.empty()        ? rv_editor_text("pane_observe.up_at_root") :
                                observe_busy_why(app);
    if (rv_editor_button(rv_editor_text("pane_observe.up"), theme, { rv_editor_look::live, up_why })) {
        std::vector<std::string> up = o.path;
        up.pop_back();
        rv_editor_observe_leave(app, rv_editor_pane_request_kind::observe_open, rv_editor_path_name(up));
    }
    rv_editor_shelf_end();
}

// What the strip says: why there is nothing to read, or what the keys listed are; the session's end is there too. No session
// yet and keys not read yet are placeholders the areas' empty rows give: no text.
std::string rv_editor_observe_text(const rv_editor_app &app, const rv_editor_answer *keys, size_t key_count)
{
    const rv_editor_session &s = app.session;
    if (!s.live()) {
        const std::string num_str = std::to_string(s.number());
        const std::string &reason = s.end_reason();
        return reason.empty() ? std::string() :
                                rv_editor_text_format("pane_observe.session_ended", std::make_format_args(num_str, reason));
    }
    if (!s.connected()) {
        return rv_editor_text("pane_observe.waiting_console");
    }
    if (s.facts().lua_budget <= 0) {
        return rv_editor_text("pane_observe.no_lua_machine");
    }
    if (keys == nullptr) {
        return {};
    }
    if (!keys->ok) {
        const std::string &error = keys->error;
        return rv_editor_text_format("pane_observe.unavailable", std::make_format_args(error));
    }
    std::string count;
    std::string shown;
    for (const auto &[k, v] : keys->fields) {
        count = k == field_count ? v : count;
        shown = k == field_shown ? v : shown;
    }
    const std::string num_keys = std::to_string(key_count);
    std::string partial;
    if (shown != count) {
        partial = rv_editor_text_format("pane_observe.keys_partial", std::make_format_args(shown, count));
    }
    const std::string &updated = rv_editor_updated_text(*keys, s);
    return rv_editor_path_text(app.observe.path) + ": " +
        rv_editor_text_format("pane_observe.keys_listed", std::make_format_args(num_keys, partial, updated));
}

// The pinned values, one row each: a click selects, the Unpin button and the table are the pane's own.
void rv_editor_observe_pins(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    const rv_editor_table_column pin_columns[pins_table_cols] = {
        { rv_editor_text("pane_observe.path_column") },
        { rv_editor_text("pane_observe.value_column") },
        { rv_editor_text("pane_observe.updated_column") },
        { "", rv_editor_table_width::fixed },
    };
    const rv_editor_table_style pin_style = { .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit };
    if (!rv_editor_table_begin("##pins", pin_columns, pins_table_cols, pin_style)) {
        return;
    }
    if (app.observe.pins.empty()) {
        rv_editor_table_row();
        rv_editor_table_cell();
        rv_editor_content_empty_row(rv_editor_text("pane_observe.pin_value"), theme);
    }
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID selected_key = ImGui::GetID("##selected_pin");
    for (const std::string &pin : app.observe.pins) {
        ImGui::PushID(pin.c_str());
        rv_editor_table_row();
        rv_editor_table_cell();
        const ImGuiID row_id = ImGui::GetID("##pin");
        // A click selects the row (the selection is the pane's own state); a pin row has no other main action.
        if (rv_editor_table_row_select("##pin", storage->GetInt(selected_key, 0) == static_cast<int>(row_id), theme).clicked) {
            storage->SetInt(selected_key, static_cast<int>(row_id));
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::AlignTextToFramePadding();
        rv_editor_table_cell_text(pin.c_str(), theme);
        const auto it = s.answers().find(std::string(observe_verb_get) + pin);
        rv_editor_table_cell();
        rv_editor_table_cell_text(it == s.answers().end() ? rv_editor_text("pane_observe.not_read_yet") :
                                                            rv_editor_value_text(it->second).c_str(),
            theme);
        rv_editor_table_cell();
        rv_editor_table_cell_text(it == s.answers().end() ? rv_editor_text("pane_observe.dash") :
                                                            rv_editor_updated_text(it->second, s).c_str(),
            theme);
        rv_editor_table_cell();
        if (rv_editor_button(rv_editor_text("pane_observe.unpin"), theme, { rv_editor_look::live, observe_busy_why(app) })) {
            rv_editor_observe_leave(app, rv_editor_pane_request_kind::observe_unpin, pin);
        }
        ImGui::PopID();
    }
    rv_editor_table_end();
}

// The keys of the shown table, one row each, with Open for a table and Pin for a value.
void rv_editor_observe_keys(rv_editor_app &app, const std::vector<rv_editor_key_row> &rows, const rv_editor_theme &theme)
{
    const rv_editor_observe &o = app.observe;
    const rv_editor_table_column key_columns[keys_table_cols] = {
        { rv_editor_text("pane_observe.key_column") },
        { rv_editor_text("pane_observe.type_column") },
        { "", rv_editor_table_width::fixed },
    };
    const rv_editor_table_style key_style = { .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit };
    if (!rv_editor_table_begin("##keys", key_columns, keys_table_cols, key_style)) {
        return;
    }
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID selected_key = ImGui::GetID("##selected_key");
    const char *const action_labels[] = { rv_editor_text("pane_observe.open"), rv_editor_text("pane_observe.pin") };
    for (const rv_editor_key_row &row : rows) {
        ImGui::PushID(row.shown.c_str());
        rv_editor_table_row();
        rv_editor_table_cell();
        const ImGuiID row_id = ImGui::GetID("##key");
        const rv_editor_table_row_result picked =
            rv_editor_table_row_select("##key", storage->GetInt(selected_key, 0) == static_cast<int>(row_id), theme);
        if (picked.clicked) {
            storage->SetInt(selected_key, static_cast<int>(row_id));
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::AlignTextToFramePadding();
        rv_editor_table_cell_text(row.shown.c_str(), theme);
        rv_editor_say_whole_text();
        rv_editor_table_cell();
        rv_editor_label(row.type.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
        rv_editor_table_cell();
        if (row.name.empty()) {
            rv_editor_label(rv_editor_text("pane_observe.dash"),
                rv_editor_label_kind::normal,
                theme,
                rv_editor_label_layout::line);
        } else if (row.type == type_table) {
            const char *open_why = observe_busy_why(app);
            // A double click or Enter on a table's row does what its Open button does.
            if (rv_editor_button_fixed(rv_editor_text("pane_observe.open"),
                    { action_labels[0], action_labels[1] },
                    theme,
                    { rv_editor_look::live, open_why }) ||
                (picked.activated && open_why == nullptr)) {
                std::vector<std::string> next = o.path;
                next.push_back(row.name);
                rv_editor_observe_leave(app, rv_editor_pane_request_kind::observe_open, rv_editor_path_name(next));
            }
        } else {
            std::vector<std::string> full = o.path;
            full.push_back(row.name);
            const std::string pin = rv_editor_path_name(full);
            const char *pin_why = observe_busy_why(app);
            if (pin_why == nullptr && std::find(o.pins.begin(), o.pins.end(), pin) != o.pins.end()) {
                pin_why = rv_editor_text("pane_observe.already_pinned");
            }
            if (rv_editor_button_fixed(rv_editor_text("pane_observe.pin"),
                    { action_labels[0], action_labels[1] },
                    theme,
                    { rv_editor_look::live, pin_why })) {
                rv_editor_observe_leave(app, rv_editor_pane_request_kind::observe_pin, pin);
            }
        }
        ImGui::PopID();
    }
    rv_editor_table_end();
}

} // namespace

rv_editor_size rv_editor_observe_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_observe_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_strip_block(),
        { rv_editor_tiled_floor(), rv_editor_tiled_floor(), rv_editor_tiled_floor() });
}

void rv_editor_pane_observe(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    const bool machine = app.project.open && s.live() && s.connected();
    const bool lua = machine && s.facts().lua_budget > 0;
    // A paused machine is read again after every frame it runs (a Step); a running one only on Refresh, and its values say
    // they are samples.
    if (lua && s.state() == rv_editor_run_state::paused && app.observe.read_frame != s.frame() &&
        !rv_editor_app_observe_waiting(app)) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::observe_read });
    }
    const auto it = lua ? s.answers().find(rv_editor_observe_request(observe_verb_keys, app.observe.path)) : s.answers().end();
    const rv_editor_answer *keys = lua && it != s.answers().end() ? &it->second : nullptr;
    const std::vector<rv_editor_key_row> rows =
        keys != nullptr && keys->ok ? rv_editor_key_rows(*keys) : std::vector<rv_editor_key_row>{};

    const float width = rv_editor_observe_content_width();
    rv_editor_observe_shelf(app, lua, theme);
    const rv_editor_message_say say = app.project.open ?
        rv_editor_message_say{ rv_editor_observe_text(app, keys, rows.size()), rv_editor_label_kind::dim, {} } :
        rv_editor_say_no_project();
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, width);
    const float strip_h = rv_editor_message_strip_height(say.text, say.buttons, strip_w);
    const float floor = rv_editor_tiled_floor();
    (void)rv_editor_tiled_area_begin("##facts_area", width, theme, { floor, floor, floor }, machine, 0.0f, strip_h);
    if (machine) {
        rv_editor_observe_facts(s, theme);
    } else {
        rv_editor_content_empty_row(rv_editor_text("pane_observe.facts_empty"), theme);
    }
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_tiled_area_begin("##pins_area", width, theme, { floor, floor }, false, 0.0f, strip_h);
    if (lua) {
        rv_editor_observe_pins(app, theme);
        rv_editor_label(rv_editor_text("pane_observe.each_value_note"),
            rv_editor_label_kind::dim,
            theme,
            rv_editor_label_layout::wrap);
    } else {
        rv_editor_content_empty_row(rv_editor_text("pane_observe.pins_empty"), theme);
    }
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_tiled_area_begin("##keys_area", width, theme, {}, false, 0.0f, strip_h);
    if (lua && keys != nullptr && keys->ok) {
        rv_editor_observe_keys(app, rows, theme);
    } else {
        rv_editor_content_empty_row(rv_editor_text("pane_observe.keys_empty"), theme);
    }
    rv_editor_tiled_area_end(theme);
    if (rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, strip_w) == 0) {
        app.open_folder_request = true;
    }
}

} // namespace rv_editor
