// The Scene viewport's geometry: the editor camera and what objects draw as.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "app/rv_editor_app.hpp"

namespace rv_editor
{

namespace
{

constexpr double rv_editor_deg = 3.14159265358979323846 / 180.0;

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

// Corner brackets of a small cube: three short legs per corner, pointing inward, plus
// a tiny axis cross through the centre. A group's marker: clearly not geometry.
void rv_editor_group_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const double h = 0.25, leg = 0.12;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                const vec3 corner = p(sx * h, sy * h, sz * h);
                e.emplace_back(corner, p(sx * h - sx * leg, sy * h, sz * h));
                e.emplace_back(corner, p(sx * h, sy * h - sy * leg, sz * h));
                e.emplace_back(corner, p(sx * h, sy * h, sz * h - sz * leg));
            }
        }
    }
    e.emplace_back(p(-0.15, 0, 0), p(0.15, 0, 0));
    e.emplace_back(p(0, -0.15, 0), p(0, 0.15, 0));
    e.emplace_back(p(0, 0, -0.15), p(0, 0, 0.15));
}

// A recognisable camera: a box body behind the origin plus a lens cone toward +Z, with an
// up mark on the lens rim so orientation reads.
void rv_editor_camera_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 b[8] = { p(-0.2, -0.2, -0.4), p(0.2, -0.2, -0.4), p(0.2, 0.2, -0.4), p(-0.2, 0.2, -0.4),
        p(-0.2, -0.2, 0), p(0.2, -0.2, 0), p(0.2, 0.2, 0), p(-0.2, 0.2, 0) };
    const int pairs[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const auto &pr : pairs) {
        e.emplace_back(b[pr[0]], b[pr[1]]);
    }
    const vec3 o = p(0, 0, 0);
    const vec3 c[4] = { p(-0.3, 0.2, 0.6), p(0.3, 0.2, 0.6), p(0.3, -0.2, 0.6), p(-0.3, -0.2, 0.6) };
    for (int i = 0; i < 4; ++i) {
        e.emplace_back(o, c[i]);
        e.emplace_back(c[i], c[(i + 1) % 4]);
    }
    e.emplace_back(p(-0.1, 0.25, 0.6), p(0, 0.35, 0.6)); // the up mark
    e.emplace_back(p(0, 0.35, 0.6), p(0.1, 0.25, 0.6));
}

// The local XY unit square (-0.5..0.5): a quad's outline and a billboard's card.
void rv_editor_quad_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 c[4] = { p(-0.5, -0.5, 0), p(0.5, -0.5, 0), p(0.5, 0.5, 0), p(-0.5, 0.5, 0) };
    for (int i = 0; i < 4; ++i) {
        e.emplace_back(c[i], c[(i + 1) % 4]);
    }
}

// A billboard: the same card, plus a short tick off its face so the card does not read as flat.
void rv_editor_billboard_edges(
    std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    rv_editor_quad_edges(e, p);
    e.emplace_back(p(0, 0, 0), p(0, 0, 0.3));
}

// A volume: the unit cube's 12 edges, never filled.
void rv_editor_volume_edges(
    std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 b[8] = { p(-0.5, -0.5, -0.5), p(0.5, -0.5, -0.5), p(0.5, 0.5, -0.5), p(-0.5, 0.5, -0.5),
        p(-0.5, -0.5, 0.5), p(0.5, -0.5, 0.5), p(0.5, 0.5, 0.5), p(-0.5, 0.5, 0.5) };
    const int pairs[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const auto &pr : pairs) {
        e.emplace_back(b[pr[0]], b[pr[1]]);
    }
}

// A diamond (octahedron), the third marker: for a kind that is neither mesh, camera nor group.
void rv_editor_other_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const double h = 0.3;
    const vec3 v[6] = { p(h, 0, 0), p(-h, 0, 0), p(0, h, 0), p(0, -h, 0), p(0, 0, h), p(0, 0, -h) };
    // Top/bottom apexes (4, 5) to each of the four equatorial points (0..3).
    for (int apex = 4; apex <= 5; ++apex) {
        for (int eq = 0; eq < 4; ++eq) {
            e.emplace_back(v[apex], v[eq]);
        }
    }
    e.emplace_back(v[0], v[2]);
    e.emplace_back(v[2], v[1]);
    e.emplace_back(v[1], v[3]);
    e.emplace_back(v[3], v[0]);
}

std::vector<std::pair<vec3, vec3>> rv_editor_object_edges(
    const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error)
{
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string &kind = scene.objects[static_cast<size_t>(index)].kind;
    auto p = [&m](double x, double y, double z) { return rv_editor_affine_point(m, { x, y, z }); };
    std::vector<std::pair<vec3, vec3>> e;
    if (kind == "mesh") {
        for (const rv_editor_tri &tri : rv_editor_object_triangles(scene, project, index, error)) {
            e.emplace_back(tri.p[0], tri.p[1]);
            e.emplace_back(tri.p[1], tri.p[2]);
            e.emplace_back(tri.p[2], tri.p[0]);
        }
    } else if (kind == "camera") {
        rv_editor_camera_edges(e, p);
    } else if (kind == "group") {
        rv_editor_group_edges(e, p);
    } else if (kind == "quad") {
        rv_editor_quad_edges(e, p);
    } else if (kind == "billboard") {
        rv_editor_billboard_edges(e, p);
    } else if (kind == "volume") {
        rv_editor_volume_edges(e, p);
    } else {
        rv_editor_other_edges(e, p);
    }
    return e;
}

std::string rv_editor_pick(
    const rv_editor_scene &scene, const rv_editor_project &project, const rv_editor_view &v, ImVec2 at)
{
    std::string best;
    float best_d = 6.0f;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        for (const auto &[a, b] : rv_editor_object_edges(scene, project, static_cast<int>(i))) {
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
