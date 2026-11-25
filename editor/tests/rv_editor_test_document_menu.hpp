#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>

#include "app/rv_editor_shell.hpp"
#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{

struct menu_fixture {
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    fs::path root = fs::temp_directory_path() / ("rv_editor_document_menu_" + std::to_string(getpid()));
    fs::path scene_path = root / "courtyard.scene.toml";
    rv_editor_pane_id scene = rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::scene);
    rv_editor_pane_id first = rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::code);
    rv_editor_pane_id second = rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::code);

    menu_fixture()
    {
        fs::create_directory(root);
        shell.app.project.open = true;
        shell.app.project.root = root;
        shell.app.project.sdk = rv_editor_sdk_state::pdkkit;
        shell.ws.layout = rv_editor_layout_make(scene);
        RV_EDITOR_CHECK(rv_editor_tile_insert(shell.ws.layout, shell.ws.layout.root, first, rv_editor_tile_dock::right) !=
            rv_editor_tile_none);
        RV_EDITOR_CHECK(rv_editor_tile_insert(shell.ws.layout,
                            rv_editor_tile_find(shell.ws.layout, scene),
                            second,
                            rv_editor_tile_dock::bottom) != rv_editor_tile_none);
        shell.app.scene = std::make_unique<rv_editor_scene_doc>();
        shell.app.scene->scene.path = scene_path;
        focus(scene);
    }

    ~menu_fixture()
    {
        std::string error;
        if (shell.app.nvim.running()) {
            RV_EDITOR_CHECK(shell.app.nvim.stop(error) == RV_OK);
        }
        fs::remove_all(root);
    }

    void focus(rv_editor_pane_id pane)
    {
        shell.ws.focused_leaf = rv_editor_tile_find(shell.ws.layout, pane);
        RV_EDITOR_CHECK(shell.ws.focused_leaf != rv_editor_tile_none);
        if (pane == first || pane == second) {
            shell.last_code = pane;
        }
    }

    void edit_scene()
    {
        rv_editor_scene_object object;
        object.id = "tree";
        rv_editor_scene_step(*shell.app.scene);
        shell.app.scene->scene.objects.push_back(object);
    }

    template <class F>
    bool wait_for(F done)
    {
        const auto until = std::chrono::steady_clock::now() + 3s;
        do {
            shell.app.nvim.update(shell.app.log);
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        } while (std::chrono::steady_clock::now() < until);
        return false;
    }

    bool start_code()
    {
        if (rv_editor_process_find("nvim").empty()) {
            std::printf("[ SKIP ] Document menu integration requires nvim on PATH\n");
            return false;
        }
        std::string error;
        const int guard = rv_editor_guard_start(error);
        RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
        if (guard != RV_OK && guard != RV_ERR_BUSY) {
            return false;
        }
        const int started = shell.app.nvim.start(root, shell.app.log, error);
        if (started != RV_OK) {
            std::fprintf(stderr, "Document menu fixture: %s\n", error.c_str());
        }
        RV_EDITOR_CHECK(started == RV_OK);
        return started == RV_OK;
    }

    bool open_code(rv_editor_pane_id pane, const fs::path &path)
    {
        const bool window_ready = wait_for([&] {
            return shell.app.nvim.window_for(pane) != 0;
        });
        RV_EDITOR_CHECK(window_ready);
        if (!window_ready) {
            return false;
        }
        const int64_t win = shell.app.nvim.window_for(pane);
        shell.app.nvim.open(win, path, 1);
        const bool buffer_ready = wait_for([&] {
            const auto *buffer = shell.app.nvim.buffer_in(win);
            return buffer != nullptr && buffer->name == path.string();
        });
        RV_EDITOR_CHECK(buffer_ready);
        return buffer_ready;
    }

    bool modify_code(rv_editor_pane_id pane, const std::string &prefix)
    {
        const int64_t win = shell.app.nvim.window_for(pane);
        // Switch windows and edit in a single command so setup does not depend on cursor redraw.
        shell.app.nvim.input("<Cmd>lua vim.api.nvim_set_current_win(" + std::to_string(win) +
            "); vim.api.nvim_buf_set_text(0, 0, 0, 0, 0, {'" + prefix + "'})<CR>");
        const bool modified = wait_for([&] {
            const auto *current = shell.app.nvim.buffer_in(win);
            return current != nullptr && current->modified;
        });
        RV_EDITOR_CHECK(modified);
        return modified;
    }

    bool open_modified_pair(const fs::path &first_path, const fs::path &second_path)
    {
        std::ofstream(first_path) << "one\n";
        std::ofstream(second_path) << "two\n";
        return start_code() && open_code(first, first_path) && open_code(second, second_path) && modify_code(first, "alpha-") &&
            modify_code(second, "beta-");
    }

    rv_editor_probe_record draw(std::string *rendered_text = nullptr)
    {
        shell.app.nvim.update(shell.app.log);
        rv_editor_probe_record record;
        rv_editor_probe_detail::sink sink(&record);
        rv_editor_item_sink_set(&sink);
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        ImGui::NewFrame();
        if (rendered_text != nullptr) {
            ImGui::LogToBuffer();
        }
        rv_editor_shell_menu(shell);
        if (shell.ws.pending.what != rv_editor_tile_action::op::none) {
            ImGui::Begin("test workspace", nullptr, ImGuiWindowFlags_NoSavedSettings);
            std::string error;
            const rv_editor_rect area = { 0, 100, 800, 300 };
            RV_EDITOR_CHECK(rv_editor_workspace_draw(
                                shell.ws,
                                rv_editor_theme_olive,
                                [](void *, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &) {},
                                rv_editor_shell_close_pane,
                                &shell,
                                area,
                                error) == RV_OK);
            ImGui::End();
        }
        if (shell.save_as_buffer != 0 && shell.ws.panes.panes[shell.save_as_pane].kind == rv_editor_pane_kind::code) {
            ImGui::SetNextWindowPos(ImVec2(0, 100));
            ImGui::SetNextWindowSize(ImVec2(800, 300));
            ImGui::Begin("test code tile", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            rv_editor_shell_code_asks(shell, shell.save_as_pane, rv_editor_theme_olive);
            ImGui::End();
        }
        if (shell.leaving != rv_editor_shell::rv_editor_leave::none) {
            ImGui::SetNextWindowPos(ImVec2(0, 100));
            ImGui::SetNextWindowSize(ImVec2(800, 300));
            ImGui::Begin("test review tile", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            rv_editor_page_review(shell, rv_editor_theme_olive);
            ImGui::End();
        }
        if (rendered_text != nullptr) {
            *rendered_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        }
        ImGui::EndFrame();
        ImGui::GetCurrentContext()->TestEngineHookItems = false;
        rv_editor_item_sink_set(nullptr);
        return record;
    }

    std::optional<ImVec2> find(std::string_view label)
    {
        const auto record = draw();
        const auto item = std::ranges::find_if(record.entries, [label](const auto &entry) {
            return std::string_view(entry.label).substr(0, entry.label.find("##")) == label;
        });
        if (item == record.entries.end()) {
            return std::nullopt;
        }
        return ImVec2((item->x0 + item->x1) / 2.0f, (item->y0 + item->y1) / 2.0f);
    }

    void click(ImVec2 at)
    {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        draw();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        draw();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        draw();
    }

    void dismiss()
    {
        click(ImVec2(1100, 500));
    }

    std::optional<ImVec2> open_item(std::initializer_list<std::string_view> labels, const char *old_menu)
    {
        dismiss();
        // S3 uses File and Edit. The integrated menu moves the same actions into Project in S11.
        for (const char *menu : { "Project", old_menu }) {
            const auto top = find(menu);
            if (!top) {
                continue;
            }
            click(*top);
            for (const auto label : labels) {
                if (auto item = find(label)) {
                    return item;
                }
            }
            dismiss();
        }
        RV_EDITOR_CHECK(false && "document action is present in the menu");
        return std::nullopt;
    }

    std::string hover(ImVec2 at)
    {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        for (int frames = 0; frames < 40; ++frames) {
            draw();
        }
        std::string text;
        draw(&text);
        return text;
    }
};

std::string read_file(const fs::path &path)
{
    std::ifstream in(path);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

bool includes(const std::string &text, std::string_view words)
{
    return text.find(words) != std::string::npos;
}

} // namespace
