#pragma once

#include <cstdint>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod_int.hpp"

namespace rv_editor
{

// A piece: triangles linked through shared vertex indices, as indices into model.triangles.
using rv_editor_piece = std::vector<uint32_t>;

// Splits a validated model's triangles into pieces. Pieces come in the order of their first triangle,
// each lists its triangles ascending, and every triangle is in exactly one piece.
std::vector<rv_editor_piece> rv_editor_model_pieces(const rv_pdklib::rv_mppcmod_int_model &model);

} // namespace rv_editor
