// The Scene viewport's geometry: the editor camera and what objects draw as.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <cmath>

#include "app/rv_editor_app.hpp"

namespace rv_editor
{

namespace
{

constexpr double rv_editor_deg = 3.14159265358979323846 / 180.0;
constexpr double rv_editor_near = 0.05;

vec3 cross(const vec3 &a, const vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

vec3 norm(const vec3 &a)
{
    const double l = std::sqrt(dot(a, a));
    return l > 1e-12 ? mul(a, 1.0 / l) : a;
}

} // namespace

vec3 sub(const vec3 &a, const vec3 &b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
vec3 add(const vec3 &a, const vec3 &b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}
vec3 mul(const vec3 &a, double s)
{
    return { a[0] * s, a[1] * s, a[2] * s };
}
double dot(const vec3 &a, const vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

vec3 rv_editor_view::to_view(const vec3 &p) const
{
    const vec3 v = sub(p, eye);
    return { dot(v, right), dot(v, up), dot(v, forward) };
}

ImVec2 rv_editor_view::to_screen(const vec3 &v) const
{
    return ImVec2(center.x + static_cast<float>(v[0] / v[2] * focal), center.y - static_cast<float>(v[1] / v[2] * focal));
}

bool rv_editor_view::point(const vec3 &p, ImVec2 &out) const
{
    const vec3 v = to_view(p);
    if (v[2] < rv_editor_near) {
        return false;
    }
    out = to_screen(v);
    return true;
}

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size)
{
    rv_editor_view v;
    const double yaw = c.yaw * rv_editor_deg, pitch = c.pitch * rv_editor_deg;
    v.forward = { std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw) };
    v.eye = sub(c.target, mul(v.forward, c.distance));
    v.right = norm(cross({ 0.0, 1.0, 0.0 }, v.forward));
    v.up = cross(v.forward, v.right);
    v.center = ImVec2(p0.x + size.x * 0.5f, p0.y + size.y * 0.5f);
    v.focal = size.y * 0.5 / std::tan(25.0 * rv_editor_deg);
    return v;
}

void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width)
{
    vec3 va = v.to_view(a), vb = v.to_view(b);
    if (va[2] < rv_editor_near && vb[2] < rv_editor_near) {
        return;
    }
    if (va[2] < rv_editor_near || vb[2] < rv_editor_near) {
        const double t = (rv_editor_near - va[2]) / (vb[2] - va[2]);
        const vec3 cut = add(va, mul(sub(vb, va), t));
        (va[2] < rv_editor_near ? va : vb) = cut;
    }
    dl->AddLine(v.to_screen(va), v.to_screen(vb), color, width);
}

float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<std::pair<vec3, vec3>> rv_editor_object_edges(const rv_editor_scene &scene, int index)
{
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string &kind = scene.objects[static_cast<size_t>(index)].kind;
    auto p = [&m](double x, double y, double z) { return rv_editor_affine_point(m, { x, y, z }); };
    std::vector<std::pair<vec3, vec3>> e;
    if (kind == "mesh") {
        const double h = 0.5;
        const vec3 c[8] = { p(-h, -h, -h), p(h, -h, -h), p(h, h, -h), p(-h, h, -h), p(-h, -h, h), p(h, -h, h),
            p(h, h, h), p(-h, h, h) };
        const int pairs[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (const auto &pr : pairs) {
            e.emplace_back(c[pr[0]], c[pr[1]]);
        }
    } else if (kind == "camera") {
        const vec3 o = p(0, 0, 0);
        const vec3 c[4] = { p(-0.3, 0.2, 0.6), p(0.3, 0.2, 0.6), p(0.3, -0.2, 0.6), p(-0.3, -0.2, 0.6) };
        for (int i = 0; i < 4; ++i) {
            e.emplace_back(o, c[i]);
            e.emplace_back(c[i], c[(i + 1) % 4]);
        }
        e.emplace_back(p(-0.1, 0.25, 0.6), p(0, 0.35, 0.6)); // the up mark
        e.emplace_back(p(0, 0.35, 0.6), p(0.1, 0.25, 0.6));
    } else {
        e.emplace_back(p(-0.3, 0, 0), p(0.3, 0, 0));
        e.emplace_back(p(0, -0.3, 0), p(0, 0.3, 0));
        e.emplace_back(p(0, 0, -0.3), p(0, 0, 0.3));
    }
    return e;
}

std::string rv_editor_pick(const rv_editor_scene &scene, const rv_editor_view &v, ImVec2 at)
{
    std::string best;
    float best_d = 6.0f;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        for (const auto &[a, b] : rv_editor_object_edges(scene, static_cast<int>(i))) {
            ImVec2 sa, sb;
            if (!v.point(a, sa) || !v.point(b, sb)) {
                continue;
            }
            const float d = rv_editor_segment_distance(at, sa, sb);
            if (d < best_d) {
                best_d = d;
                best = scene.objects[i].id;
            }
        }
    }
    return best;
}

// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene, const std::string &selected, const rv_editor_view &v,
    ImVec2 mouse, double length)
{
    const int at = rv_editor_scene_find(scene, selected);
    if (at < 0) {
        return -1;
    }
    const vec3 o = rv_editor_affine_point(rv_editor_scene_world(scene, at), { 0, 0, 0 });
    ImVec2 so;
    if (!v.point(o, so)) {
        return -1;
    }
    for (int a = 0; a < 3; ++a) {
        vec3 tip = o;
        tip[static_cast<size_t>(a)] += length;
        ImVec2 st;
        if (v.point(tip, st) && rv_editor_segment_distance(mouse, so, st) < 6.0f) {
            return a;
        }
    }
    return -1;
}

} // namespace rv_editor
