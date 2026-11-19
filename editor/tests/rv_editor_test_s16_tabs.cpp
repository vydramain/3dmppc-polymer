// S16, R15/AC1: the Scene tile's asset tabs (picture, sound, model) draw into the content area at the Scene's minimum: a
// 120-character file name and a long error stay whole or scroll, and can be read to the end; the status band over the
// viewport takes two lines at the minimum width; the read-only note, the error and the status fit the viewport together.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s16.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s16;

namespace
{

using vertex = std::array<float, 5>; // x, y, u, v, colour

const std::string long_name(120, 'f');

std::vector<vertex> vertices()
{
    ImGui::Render();
    std::vector<vertex> out;
    const ImDrawData *data = ImGui::GetDrawData();
    for (int i = 0; data != nullptr && i < data->CmdLists.Size; ++i) {
        for (const ImDrawVert &v : data->CmdLists[i]->VtxBuffer) {
            out.push_back({ v.pos.x, v.pos.y, v.uv.x, v.uv.y, static_cast<float>(v.col) });
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A glTF whose one triangle lies far outside Q20.12, so the reader leaves it out and says so.
void write_far_model(const fs::path &file)
{
    const float far = 1.0e9f;
    const float v[9] = { far, 0, 0, 0, far, 0, 0, 0, far };
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(v);
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < sizeof(v); i += 3) {
        const uint32_t n = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
        for (int k = 3; k >= 0; --k) {
            b64 += table[(n >> (6 * k)) & 63];
        }
    }
    std::ofstream(file) << R"({"asset":{"version":"2.0"},"scene":0,"scenes":{"nodes":[0]},)"
                           R"("nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
                           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
                           R"("min":[0,0,0],"max":[1e9,1e9,1e9]}],)"
                           R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
                           R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)"
                        << b64 << R"("}]})";
}

// The Scene tile in `fx` with one asset tab of `kind` in front, named `long_name` + ext.
void asset_tab(fx &f, rv_editor_scene_tab_kind kind, const char *ext)
{
    f.base();
    const fs::path file = f.where / (long_name + ext);
    f.shell.ui.scene_tabs.tabs.push_back({ file, kind, 0.0 });
    f.shell.ui.scene_tabs.front = 1;
}

// "" or the first problem of the tile drawn in `min`: an element outside the tile, no content area, a scrolled element
// narrower than its own text (its end could not be read by scrolling).
std::string tab_problem(fx &f, stable_tiles::minimum min)
{
    const rv_editor_probe_record r = settled(f, min.w, min.h);
    if (const std::string bad = rv_editor_fits_whole(r, ImRect(0, 0, min.w, min.h)); !bad.empty()) {
        return bad;
    }
    if (content_area(r) == nullptr) {
        return "the tab's content is not in a content area";
    }
    for (const auto &e : r.entries) {
        if (e.in_scroll && e.text_w > e.x1 - e.x0) {
            return rv_editor_probe_detail::name(e) + " is narrower than its text " + std::to_string(e.text_w);
        }
    }
    return "";
}

// Clicks the button named `label` of the pane.
void click(fx &f, const char *label, stable_tiles::minimum min)
{
    const auto *b = find(settled(f, min.w, min.h), label);
    RV_EDITOR_CHECK(b != nullptr);
    if (b == nullptr) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((b->x0 + b->x1) / 2, (b->y0 + b->y1) / 2);
    settled(f, min.w, min.h);
    io.AddMouseButtonEvent(0, true);
    f.draw(min.w, min.h);
    io.AddMouseButtonEvent(0, false);
    f.draw(min.w, min.h);
    f.draw(min.w, min.h);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    f.draw(min.w, min.h);
}

} // namespace

// Picture tab: the load error names the long file; the content is in the content area and can be read to its end.
RV_EDITOR_TEST(rv_editor_test_s16_picture_tab_content_is_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::scene);
    fx f(gui, rv_editor_pane_kind::scene);
    asset_tab(f, rv_editor_scene_tab_kind::picture, ".png");
    const std::string bad = tab_problem(f, min);
    report("picture tab", bad);
    RV_EDITOR_CHECK(bad.empty());
}

// Sound tab: the long name and the error of a failed play.
RV_EDITOR_TEST(rv_editor_test_s16_sound_tab_content_is_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::scene);
    fx f(gui, rv_editor_pane_kind::scene);
    asset_tab(f, rv_editor_scene_tab_kind::sound, ".wav");
    const std::string before = tab_problem(f, min);
    report("sound tab", before);
    RV_EDITOR_CHECK(before.empty());
    click(f, rv_editor_text("pane_scene_tabs.button_play"), min); // the file is not there: the play fails with an error
    const std::string after = tab_problem(f, min);
    report("sound tab after a failed play", after);
    RV_EDITOR_CHECK(after.empty());
}

// Model tab: the read error of a missing file, and the warning about the triangles the reader left out.
RV_EDITOR_TEST(rv_editor_test_s16_model_tab_content_is_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::scene);
    fx f(gui, rv_editor_pane_kind::scene);
    asset_tab(f, rv_editor_scene_tab_kind::model, ".gltf");
    const std::string missing = tab_problem(f, min);
    report("model tab, missing file", missing);
    RV_EDITOR_CHECK(missing.empty());
    asset_tab(f, rv_editor_scene_tab_kind::model, ".gltf");
    write_far_model(f.where / (long_name + ".gltf"));
    const std::string dropped = tab_problem(f, min);
    report("model tab, dropped triangles", dropped);
    RV_EDITOR_CHECK(dropped.empty());
}

// R15: the status over the viewport (camera, seek hint or mesh error, game) takes two lines at the Scene's minimum: the
// viewport is as wide as the longest status needs for two lines, with the numbers at their limit.
RV_EDITOR_TEST(rv_editor_test_s16_scene_status_takes_two_lines_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::scene);
    float need = 0.0f;
    gui.frame([&] {
        const double yaw = 999999.0, pitch = 999999.0, distance = 999999.9;
        const std::string mesh_error = "a.gltf: no triangles";
        std::vector<std::string> texts;
        for (const char *seek : { "", rv_editor_text("scene_viewport.seek_hint") }) {
            const std::string seek_msg = seek;
            const std::string camera =
                rv_editor_text_format("scene_viewport.camera_status", std::make_format_args(yaw, pitch, distance, seek_msg));
            for (const char *game : { rv_editor_text("scene_viewport.game_not_running"),
                     rv_editor_text("scene_viewport.game_read_scene"),
                     rv_editor_text("scene_viewport.game_not_read_scene") }) {
                texts.push_back(
                    rv_editor_text_format("scene_viewport.status_line_format", std::make_format_args(camera, game)));
                texts.push_back(rv_editor_text_format("scene_viewport.mesh_placeholder_format",
                    std::make_format_args(camera, game, mesh_error)));
            }
        }
        need = rv_editor_picture_min_width(texts);
    });
    fx f(gui, rv_editor_pane_kind::scene);
    f.states[2]();
    const auto r = settled(f, min.w, min.h);
    const auto *viewport = find(r, "##viewport");
    RV_EDITOR_CHECK(viewport != nullptr);
    if (viewport == nullptr) {
        return;
    }
    const float width = viewport->x1 - viewport->x0;
    if (width < need) {
        std::fprintf(stderr,
            "  the viewport is %.1f px wide at the minimum, two lines of the longest status need %.1f\n",
            width,
            need);
    }
    RV_EDITOR_CHECK(width >= need);
}

// R15: the read-only note, the error and the status, two lines each, are all inside the viewport at the minimum.
RV_EDITOR_TEST(rv_editor_test_s16_three_bands_fit_the_viewport_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::scene);
    fx f(gui, rv_editor_pane_kind::scene);
    f.states[2]();
    settled(f, min.w, min.h);
    f.draw(min.w, min.h);
    const std::vector<vertex> base = vertices();
    f.states[2]();
    f.app().scene->scene.read_only = words(300);
    f.app().scene_error = "scenes/b.scene.toml: " + words(300);
    f.shell.ui.scene_camera.seeking = true;
    const auto r = settled(f, min.w, min.h);
    f.draw(min.w, min.h);
    f.shell.ui.scene_camera.seeking = false;
    const std::vector<vertex> now = vertices();
    std::vector<vertex> added;
    std::set_difference(now.begin(), now.end(), base.begin(), base.end(), std::back_inserter(added));
    const auto *viewport = find(r, "##viewport");
    RV_EDITOR_CHECK(viewport != nullptr);
    RV_EDITOR_CHECK(!added.empty());
    if (viewport == nullptr) {
        return;
    }
    for (const vertex &v : added) {
        // The Seek button of the shelf above the viewport also changes with the camera; only what is at or under the
        // viewport's top belongs to the bands.
        if (v[1] < viewport->y0 - 1.0f) {
            continue;
        }
        if (v[0] < viewport->x0 - 1.0f || v[0] > viewport->x1 + 1.0f || v[1] > viewport->y1 + 1.0f) {
            std::fprintf(stderr,
                "  a band vertex at %.1f,%.1f is outside the viewport %s\n",
                v[0],
                v[1],
                rv_editor_probe_detail::rect(*viewport).c_str());
            RV_EDITOR_CHECK(false);
            return;
        }
    }
}
