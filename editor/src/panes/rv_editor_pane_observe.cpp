// Observe: what the running session says about itself, and its persistent Lua
// state read through `keys` and `get` (read-only). Session: what ran, and
// after it ends, how.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

void rv_editor_dim(const std::string &text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string rv_editor_clock(std::chrono::system_clock::time_point at)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(at);
    std::tm tm{};
    localtime_r(&t, &tm);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count() % 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

// One label and its value on a row of a two-column table.
void rv_editor_fact(const char *label, const std::string &value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

namespace
{

// The request for the value or the keys at `path`: "get a b", "keys a b".
std::string rv_editor_request(const char *verb, const std::vector<std::string> &path)
{
    std::string out = verb;
    for (const std::string &key : path) {
        out += " " + key;
    }
    return out;
}

std::string rv_editor_path_text(const std::vector<std::string> &path)
{
    std::string out = "state";
    for (const std::string &key : path) {
        out += "." + key;
    }
    return out;
}

// What a `get` answer holds, as a person reads it.
std::string rv_editor_value_text(const rv_editor_answer &a)
{
    if (!a.ok) {
        const std::string &error = a.error;
        return rv_editor_text_format("pane_observe.value_unavailable",
            std::make_format_args(error));
    }
    const auto field = [&a](std::string_view key) {
        for (const auto &[k, v] : a.fields) {
            if (k == key) {
                return v;
            }
        }
        return std::string();
    };
    const std::string type = field("type");
    if (field("found") != "1") {
        return rv_editor_text("pane_observe.value_absent");
    }
    if (type == "boolean") {
        return field("value") == "1" ? rv_editor_text("pane_observe.value_true") :
                                       rv_editor_text("pane_observe.value_false");
    }
    if (type == "string") {
        return "\"" + rv_editor_hex_decode(field("value")) + "\"";
    }
    if (type == "table") {
        const std::string &count = field("count");
        return rv_editor_text_format("pane_observe.value_table",
            std::make_format_args(count));
    }
    if (type == "number") {
        return field("value");
    }
    return type;
}

// When an answer was true: the frame it saw on a paused machine, otherwise the
// moment it was read from a running one; never a claim that answers share a frame.
std::string rv_editor_updated_text(const rv_editor_answer &a, const rv_editor_session &s)
{
    if (!a.frame_exact) {
        const std::string &clock_str = rv_editor_clock(a.at);
        return rv_editor_text_format("pane_observe.updated_sampled",
            std::make_format_args(clock_str));
    }
    const bool current = s.state() == rv_editor_run_state::paused && a.frame == s.frame();
    const auto frame_num = std::to_string(a.frame);
    const char *frame_key = current ? "pane_observe.updated_frame" :
                                      "pane_observe.updated_frame_stale";
    return rv_editor_text_format(frame_key, std::make_format_args(frame_num));
}

struct rv_editor_key_row
{
    std::string name;    // as a request spells it; empty when a request cannot
    std::string shown;   // as the list shows it
    std::string type;
};

// `keys` answers each entry as s<hex>:<type>, i<n>:<type> or x:<type>.
std::vector<rv_editor_key_row> rv_editor_key_rows(const rv_editor_answer &a)
{
    std::vector<rv_editor_key_row> rows;
    std::string list;
    for (const auto &[k, v] : a.fields) {
        if (k == "keys") {
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
        if (entry[0] == 's') {
            row.shown = rv_editor_hex_decode(key);
            // The channel splits a request at spaces: such a key cannot be asked for.
            const bool plain = !row.shown.empty() && row.shown.find_first_of(" \t\n") == std::string::npos;
            row.name = plain ? row.shown : std::string();
        } else if (entry[0] == 'i') {
            row.shown = "[" + key + "]";
            row.name = key;
        } else {
            row.shown = rv_editor_text("pane_observe.value_unknown_key");
        }
        rows.push_back(row);
    }
    return rows;
}

void rv_editor_observe_read(rv_editor_app &app)
{
    rv_editor_observe &o = app.observe;
    app.session.query(rv_editor_request("keys", o.path), app.log);
    for (const std::string &pin : o.pins) {
        app.session.query("get " + pin, app.log);
    }
    o.read_frame = app.session.frame();
}

void rv_editor_observe_facts(const rv_editor_session &s)
{
    const rv_editor_session_facts &f = s.facts();
    if (!ImGui::BeginTable("##facts", 2, ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    const auto num = std::to_string(s.number());
    const auto state = rv_editor_run_state_name(s.state());
    const auto start_at = rv_editor_clock(s.started_at());
    rv_editor_fact(rv_editor_text("pane_observe.fact_session"),
        rv_editor_text_format("pane_observe.fact_session_value",
            std::make_format_args(num, state, start_at)));
    const auto frame = std::to_string(s.frame());
    const char *frame_key = s.state() == rv_editor_run_state::paused ?
        "pane_observe.fact_frame_paused" :
        "pane_observe.fact_frame_running";
    rv_editor_fact(rv_editor_text("pane_observe.fact_frame"),
        rv_editor_text_format(frame_key, std::make_format_args(frame)));
    const char *disc_key = f.medium == "live" ?
        "pane_observe.fact_disc_directory" :
        "pane_observe.fact_disc_image";
    rv_editor_fact(rv_editor_text("pane_observe.fact_disc"),
        rv_editor_text_format(disc_key, std::make_format_args(f.disc)));
    const auto &pdk = f.pdk;
    rv_editor_fact(rv_editor_text("pane_observe.fact_code_hash"),
        rv_editor_text_format("pane_observe.fact_code_hash_value",
            std::make_format_args(f.code_hash, pdk)));
    if (f.lua_budget > 0) {
        const auto rev = std::to_string(f.revision);
        std::string entry_val;
        if (f.revision != f.first_revision) {
            const auto first_rev = std::to_string(f.first_revision);
            entry_val = rv_editor_text_format("pane_observe.fact_entry_reloaded",
                std::make_format_args(rev, first_rev));
        } else {
            entry_val = rv_editor_text_format("pane_observe.fact_entry_as_loaded",
                std::make_format_args(rev));
        }
        rv_editor_fact(rv_editor_text("pane_observe.fact_entry_script"), entry_val);
        const auto lua_used = std::to_string(f.lua_used);
        const auto lua_budget = std::to_string(f.lua_budget);
        rv_editor_fact(rv_editor_text("pane_observe.fact_lua_memory"),
            rv_editor_text_format("pane_observe.fact_lua_memory_value",
                std::make_format_args(lua_used, lua_budget)));
    }
    const auto facts_at = rv_editor_clock(f.at);
    rv_editor_fact(rv_editor_text("pane_observe.fact_facts_from"),
        rv_editor_text_format("pane_observe.fact_facts_from_value",
            std::make_format_args(facts_at)));
    ImGui::EndTable();
}

} // namespace

void rv_editor_pane_observe(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    rv_editor_observe &o = app.observe;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (!s.live()) {
        rv_editor_dim(rv_editor_text("pane_observe.no_session"));
        if (!s.end_reason().empty()) {
            const std::string num_str = std::to_string(s.number());
            const std::string &reason = s.end_reason();
            rv_editor_dim(rv_editor_text_format("pane_observe.session_ended",
                std::make_format_args(num_str, reason)));
        }
        return;
    }
    if (!s.connected()) {
        rv_editor_dim(rv_editor_text("pane_observe.waiting_console"));
        return;
    }

    // No controls row at the top: Refresh and Up sit inside the content
    // between its sections, so the whole pane is one well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        rv_editor_observe_facts(s);
        if (rv_editor_button(rv_editor_text("pane_observe.refresh"), theme)) {
            s.refresh(app.log);
            rv_editor_observe_read(app);
        }
        if (s.facts().lua_budget <= 0) {
            rv_editor_dim(rv_editor_text("pane_observe.no_lua_machine"));
            return;
        }

        // A paused machine is read again after every frame it runs (a Step); a
        // running one only on Refresh, and its values say they are samples.
        if (s.state() == rv_editor_run_state::paused && o.read_frame != s.frame()) {
            rv_editor_observe_read(app);
        }
        rv_editor_dim(rv_editor_text("pane_observe.each_value_note"));

        ImGui::SeparatorText(rv_editor_text("pane_observe.pinned"));
        if (o.pins.empty()) {
            rv_editor_dim(rv_editor_text("pane_observe.pin_value"));
        } else if (ImGui::BeginTable("##pins", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(rv_editor_text("pane_observe.path_column"));
            ImGui::TableSetupColumn(rv_editor_text("pane_observe.value_column"));
            ImGui::TableSetupColumn(rv_editor_text("pane_observe.updated_column"));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            std::string unpin;
            for (const std::string &pin : o.pins) {
                ImGui::PushID(pin.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pin.c_str());
                const auto it = s.answers().find("get " + pin);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(it == s.answers().end() ? rv_editor_text("pane_observe.not_read_yet") :
                                                                 rv_editor_value_text(it->second).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(it == s.answers().end() ? rv_editor_text("pane_observe.dash") :
                                                                 rv_editor_updated_text(it->second, s).c_str());
                ImGui::TableNextColumn();
                if (rv_editor_button(rv_editor_text("pane_observe.unpin"), theme)) {
                    unpin = pin;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
            std::erase(o.pins, unpin);
        }

        ImGui::SeparatorText(rv_editor_path_text(o.path).c_str());
        if (!o.path.empty()) {
            if (rv_editor_button(rv_editor_text("pane_observe.up"), theme)) {
                o.path.pop_back();
                rv_editor_observe_read(app);
            }
        }
        const auto it = s.answers().find(rv_editor_request("keys", o.path));
        if (it == s.answers().end()) {
            rv_editor_dim(rv_editor_text("pane_observe.not_read_refresh"));
            return;
        }
        const rv_editor_answer &a = it->second;
        if (!a.ok) {
            const std::string &error = a.error;
            rv_editor_dim(rv_editor_text_format("pane_observe.unavailable",
                std::make_format_args(error)));
            return;
        }
        const std::vector<rv_editor_key_row> rows = rv_editor_key_rows(a);
        std::string count;
        std::string shown;
        for (const auto &[k, v] : a.fields) {
            count = k == "count" ? v : count;
            shown = k == "shown" ? v : shown;
        }
        const std::string num_keys = std::to_string(rows.size());
        std::string partial;
        if (shown != count) {
            partial = rv_editor_text_format("pane_observe.keys_partial",
                std::make_format_args(shown, count));
        }
        const std::string &updated = rv_editor_updated_text(a, s);
        rv_editor_dim(rv_editor_text_format("pane_observe.keys_listed",
            std::make_format_args(num_keys, partial, updated)));
        if (!ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            return;
        }
        ImGui::TableSetupColumn(rv_editor_text("pane_observe.key_column"));
        ImGui::TableSetupColumn(rv_editor_text("pane_observe.type_column"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        std::vector<std::string> open;
        for (const rv_editor_key_row &row : rows) {
            ImGui::PushID(row.shown.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.shown.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.type.c_str());
            ImGui::TableNextColumn();
            if (row.name.empty()) {
                ImGui::TextUnformatted(rv_editor_text("pane_observe.dash"));
            } else if (row.type == "table") {
                if (rv_editor_button(rv_editor_text("pane_observe.open"), theme)) {
                    open = o.path;
                    open.push_back(row.name);
                }
            } else {
                std::string pin = row.name;
                for (auto p = o.path.rbegin(); p != o.path.rend(); ++p) {
                    pin = *p + " " + pin;
                }
                const bool pinned = std::find(o.pins.begin(), o.pins.end(), pin) != o.pins.end();
                const char *tooltip = pinned ? rv_editor_text("pane_observe.already_pinned") : nullptr;
                if (rv_editor_button(rv_editor_text("pane_observe.pin"), theme,
                        { rv_editor_look::live, tooltip })) {
                    o.pins.push_back(pin);
                    app.session.query("get " + pin, app.log);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!open.empty()) {
            o.path = open;
            rv_editor_observe_read(app);
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
