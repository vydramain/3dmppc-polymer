#include "scene/rv_editor_model_pieces.hpp"

#include <cstddef>

namespace rv_editor
{

namespace
{

// Marks a root no piece has claimed yet.
constexpr size_t no_piece = static_cast<size_t>(-1);

// The root of v's set; halves the path as it climbs, so later lookups stay short.
uint32_t find_root(std::vector<uint32_t> &parent, uint32_t v)
{
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
    }
    return v;
}

// Joins the sets of a and b.
void join(std::vector<uint32_t> &parent, uint32_t a, uint32_t b)
{
    parent[find_root(parent, a)] = find_root(parent, b);
}

} // namespace

std::vector<rv_editor_piece> rv_editor_model_pieces(const rv_pdklib::rv_mppcmod_int_model &model)
{
    // Every vertex starts alone; a triangle joins its three vertices into one set.
    std::vector<uint32_t> parent(model.vertices.size());
    for (uint32_t v = 0; v < parent.size(); ++v) {
        parent[v] = v;
    }
    for (const rv_pdklib::rv_mppcmod_triangle &tri : model.triangles) {
        join(parent, tri.index[0], tri.index[1]);
        join(parent, tri.index[0], tri.index[2]);
    }

    // Triangles are visited in order, so a set's piece is numbered by its first triangle.
    std::vector<size_t> piece_of_root(parent.size(), no_piece);
    std::vector<rv_editor_piece> pieces;
    for (uint32_t t = 0; t < model.triangles.size(); ++t) {
        const uint32_t root = find_root(parent, model.triangles[t].index[0]);
        if (piece_of_root[root] == no_piece) {
            piece_of_root[root] = pieces.size();
            pieces.emplace_back();
        }
        pieces[piece_of_root[root]].push_back(t);
    }
    return pieces;
}

} // namespace rv_editor
