// Scene edits: each one a step of undo, the tree kept free of cycles, a reparent
// that keeps the world transform only when the file can hold it.

#include "scene/rv_editor_scene_edit.hpp"

#include <algorithm>
#include <cmath>

namespace rv_editor
{

namespace
{

constexpr double rv_editor_rad = 3.14159265358979323846 / 180.0;

using mat3 = std::array<std::array<double, 3>, 3>;

mat3 rv_editor_mat3_mul(const mat3 &a, const mat3 &b)
{
    mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
    return r;
}

// R = Ry * Rx * Rz, the order of pdklib/rv_scene (yaw, pitch, roll).
mat3 rv_editor_rotation(const rv_editor_vec3 &deg)
{
    const double x = deg[0] * rv_editor_rad, y = deg[1] * rv_editor_rad, z = deg[2] * rv_editor_rad;
    const mat3 rx{ { { 1, 0, 0 }, { 0, std::cos(x), -std::sin(x) }, { 0, std::sin(x), std::cos(x) } } };
    const mat3 ry{ { { std::cos(y), 0, std::sin(y) }, { 0, 1, 0 }, { -std::sin(y), 0, std::cos(y) } } };
    const mat3 rz{ { { std::cos(z), -std::sin(z), 0 }, { std::sin(z), std::cos(z), 0 }, { 0, 0, 1 } } };
    return rv_editor_mat3_mul(ry, rv_editor_mat3_mul(rx, rz));
}

rv_editor_affine rv_editor_affine_inverse(const rv_editor_affine &a, bool &ok)
{
    const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
        a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    ok = std::fabs(det) > 1e-12;
    rv_editor_affine r{};
    if (!ok) {
        return r;
    }
    const double inv = 1.0 / det;
    r[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * inv;
    r[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
    r[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
    r[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * inv;
    r[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
    r[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
    r[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * inv;
    r[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
    r[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
    for (int i = 0; i < 3; ++i) {
        r[i][3] = -(r[i][0] * a[0][3] + r[i][1] * a[1][3] + r[i][2] * a[2][3]);
    }
    return r;
}

// T R S back out of a matrix; false when it holds a shear, which T R S cannot.
bool rv_editor_decompose(const rv_editor_affine &m, rv_editor_scene_object &o)
{
    rv_editor_vec3 s{};
    mat3 r{};
    for (int j = 0; j < 3; ++j) {
        s[j] = std::sqrt(m[0][j] * m[0][j] + m[1][j] * m[1][j] + m[2][j] * m[2][j]);
        if (s[j] < 1e-12) {
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            r[i][j] = m[i][j] / s[j];
        }
    }
    const double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
        r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.0) {
        s[0] = -s[0];
        for (int i = 0; i < 3; ++i) {
            r[i][0] = -r[i][0];
        }
    }
    const double sx = std::fmax(-1.0, std::fmin(1.0, -r[1][2]));
    rv_editor_vec3 deg{};
    deg[0] = std::asin(sx);
    if (std::fabs(sx) < 0.999999) {
        deg[1] = std::atan2(r[0][2], r[2][2]);
        deg[2] = std::atan2(r[1][0], r[1][1]);
    } else {
        deg[1] = std::atan2(-r[2][0], r[0][0]); // pitch at +-90: roll folds into yaw
        deg[2] = 0.0;
    }
    for (double &a : deg) {
        a /= rv_editor_rad;
    }
    rv_editor_scene_object t = o;
    t.position = { m[0][3], m[1][3], m[2][3] };
    t.rotation = deg;
    t.scale = s;
    // Rebuilt, it must be the same matrix: otherwise the source had a shear.
    const rv_editor_affine back = rv_editor_scene_local(t);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::fabs(back[i][j] - m[i][j]) > 1e-6 * (1.0 + std::fabs(m[i][j]))) {
                return false;
            }
        }
    }
    o.position = t.position;
    o.rotation = t.rotation;
    o.scale = t.scale;
    return true;
}

void rv_editor_scene_subtree(const rv_editor_scene &scene, const std::string &id, std::vector<int> &out)
{
    const int at = rv_editor_scene_find(scene, id);
    if (at < 0) {
        return;
    }
    out.push_back(at);
    for (const rv_editor_scene_object &o : scene.objects) {
        if (o.parent == id) {
            rv_editor_scene_subtree(scene, o.id, out);
        }
    }
}

} // namespace

rv_editor_affine rv_editor_affine_mul(const rv_editor_affine &a, const rv_editor_affine &b)
{
    rv_editor_affine r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0);
        }
    }
    return r;
}

std::array<double, 3> rv_editor_affine_point(const rv_editor_affine &a, const std::array<double, 3> &p)
{
    return { a[0][0] * p[0] + a[0][1] * p[1] + a[0][2] * p[2] + a[0][3],
        a[1][0] * p[0] + a[1][1] * p[1] + a[1][2] * p[2] + a[1][3],
        a[2][0] * p[0] + a[2][1] * p[1] + a[2][2] * p[2] + a[2][3] };
}

std::array<double, 3> rv_editor_affine_solve(const rv_editor_affine &a, const std::array<double, 3> &dir)
{
    bool ok = false;
    const rv_editor_affine inv = rv_editor_affine_inverse(a, ok);
    if (!ok) {
        return dir;
    }
    return { inv[0][0] * dir[0] + inv[0][1] * dir[1] + inv[0][2] * dir[2],
        inv[1][0] * dir[0] + inv[1][1] * dir[1] + inv[1][2] * dir[2],
        inv[2][0] * dir[0] + inv[2][1] * dir[1] + inv[2][2] * dir[2] };
}

rv_editor_affine rv_editor_scene_local(const rv_editor_scene_object &o)
{
    const mat3 r = rv_editor_rotation(o.rotation);
    rv_editor_affine m{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m[i][j] = r[i][j] * o.scale[j];
        }
        m[i][3] = o.position[i];
    }
    return m;
}

rv_editor_affine rv_editor_scene_world(const rv_editor_scene &scene, int index)
{
    rv_editor_affine m = rv_editor_scene_local(scene.objects[static_cast<size_t>(index)]);
    int p = rv_editor_scene_find(scene, scene.objects[static_cast<size_t>(index)].parent);
    for (size_t steps = 0; p >= 0 && steps < scene.objects.size(); ++steps) {
        m = rv_editor_affine_mul(rv_editor_scene_local(scene.objects[static_cast<size_t>(p)]), m);
        p = rv_editor_scene_find(scene, scene.objects[static_cast<size_t>(p)].parent);
    }
    return m;
}

bool rv_editor_scene_under(const rv_editor_scene &scene, const std::string &id, const std::string &ancestor)
{
    std::string at = id;
    for (size_t steps = 0; !at.empty() && steps <= scene.objects.size(); ++steps) {
        if (at == ancestor) {
            return true;
        }
        const int i = rv_editor_scene_find(scene, at);
        at = i < 0 ? std::string() : scene.objects[static_cast<size_t>(i)].parent;
    }
    return false;
}

void rv_editor_scene_step(rv_editor_scene_doc &doc)
{
    doc.undo.push_back(doc.scene.objects);
    doc.redo.clear();
    doc.dirty = true;
}

bool rv_editor_scene_undo(rv_editor_scene_doc &doc)
{
    if (doc.undo.empty()) {
        return false;
    }
    doc.redo.push_back(std::move(doc.scene.objects));
    doc.scene.objects = std::move(doc.undo.back());
    doc.undo.pop_back();
    doc.dirty = true;
    return true;
}

bool rv_editor_scene_redo(rv_editor_scene_doc &doc)
{
    if (doc.redo.empty()) {
        return false;
    }
    doc.undo.push_back(std::move(doc.scene.objects));
    doc.scene.objects = std::move(doc.redo.back());
    doc.redo.pop_back();
    doc.dirty = true;
    return true;
}

std::string rv_editor_scene_add(rv_editor_scene_doc &doc, const std::string &kind, const std::string &parent)
{
    rv_editor_scene_step(doc);
    rv_editor_scene_object o;
    o.id = rv_editor_scene_new_id(doc.scene);
    o.kind = kind;
    o.name = kind == "camera" ? "Camera" : kind == "mesh" ? "Box" : kind == "quad" ? "Quad"
        : kind == "billboard"                                                    ? "Billboard"
        : kind == "volume"                                                       ? "Volume"
                                                                                  : "Group";
    o.parent = rv_editor_scene_find(doc.scene, parent) >= 0 ? parent : std::string();
    doc.scene.objects.push_back(o);
    doc.selected = o.id;
    return o.id;
}

void rv_editor_scene_delete(rv_editor_scene_doc &doc, const std::string &id)
{
    std::vector<int> gone;
    rv_editor_scene_subtree(doc.scene, id, gone);
    if (gone.empty()) {
        return;
    }
    rv_editor_scene_step(doc);
    std::vector<std::string> ids;
    for (const int i : gone) {
        ids.push_back(doc.scene.objects[static_cast<size_t>(i)].id);
    }
    std::erase_if(doc.scene.objects, [&ids](const rv_editor_scene_object &o) {
        return std::find(ids.begin(), ids.end(), o.id) != ids.end();
    });
    if (std::find(ids.begin(), ids.end(), doc.selected) != ids.end()) {
        doc.selected.clear();
    }
}

std::string rv_editor_scene_duplicate(rv_editor_scene_doc &doc, const std::string &id)
{
    std::vector<int> copy;
    rv_editor_scene_subtree(doc.scene, id, copy);
    if (copy.empty()) {
        return {};
    }
    rv_editor_scene_step(doc);
    // New ids first, then parents mapped onto them: the copy is a tree of its own.
    std::vector<std::pair<std::string, std::string>> ids;
    std::vector<rv_editor_scene_object> made;
    for (const int i : copy) {
        rv_editor_scene_object o = doc.scene.objects[static_cast<size_t>(i)];
        const std::string old = o.id;
        o.id = rv_editor_scene_new_id(doc.scene);
        ids.emplace_back(old, o.id);
        made.push_back(o);
        doc.scene.objects.push_back(o); // reserves the id against the next one
    }
    doc.scene.objects.resize(doc.scene.objects.size() - made.size());
    for (rv_editor_scene_object &o : made) {
        for (const auto &[old, now] : ids) {
            if (o.parent == old) {
                o.parent = now;
            }
        }
    }
    made.front().name += " copy";
    doc.scene.objects.insert(doc.scene.objects.end(), made.begin(), made.end());
    doc.selected = made.front().id;
    return made.front().id;
}

bool rv_editor_scene_reparent(rv_editor_scene_doc &doc, const std::string &id, const std::string &parent,
    bool keep_world, std::string &why)
{
    const int at = rv_editor_scene_find(doc.scene, id);
    if (at < 0) {
        why = "no such object";
        return false;
    }
    if (!parent.empty() && rv_editor_scene_find(doc.scene, parent) < 0) {
        why = "no such parent";
        return false;
    }
    if (!parent.empty() && rv_editor_scene_under(doc.scene, parent, id)) {
        why = "an object cannot go under itself or its own children";
        return false;
    }
    rv_editor_scene_object moved = doc.scene.objects[static_cast<size_t>(at)];
    if (keep_world) {
        const rv_editor_affine world = rv_editor_scene_world(doc.scene, at);
        rv_editor_affine local = world;
        const int p = rv_editor_scene_find(doc.scene, parent);
        if (p >= 0) {
            bool ok = false;
            local = rv_editor_affine_mul(rv_editor_affine_inverse(rv_editor_scene_world(doc.scene, p), ok), world);
            if (!ok) {
                why = "the new parent's scale is zero";
                return false;
            }
        }
        if (!rv_editor_decompose(local, moved)) {
            why = "keeping where it is would need a shear under that parent, which position, rotation and scale "
                  "cannot hold; Reparent (Keep Local Values) moves it without";
            return false;
        }
    }
    moved.parent = parent;
    rv_editor_scene_step(doc);
    doc.scene.objects[static_cast<size_t>(at)] = moved;
    return true;
}

} // namespace rv_editor
