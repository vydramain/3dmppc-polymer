#pragma once

// Internal layout file format header for the write and read sources; not for the rest of the editor.

#include "layout/rv_editor_tile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

namespace layout_format
{

// Layout file format syntax and keywords.
constexpr std::string_view layout_header_prefix = "3dmppc-editor-layout ";
constexpr std::string_view layout_field_sep = " ";
constexpr std::string_view layout_line_end = "\n";
constexpr std::string_view layout_keyword_pane = "pane";
constexpr std::string_view layout_keyword_node = "node";
constexpr std::string_view layout_keyword_root = "root";
constexpr std::string_view layout_keyword_maximized = "maximized";
constexpr std::string_view layout_node_type_free = "free";
constexpr std::string_view layout_node_type_leaf = "leaf";
constexpr std::string_view layout_node_type_split = "split";
constexpr std::string_view layout_axis_x = "x";
constexpr std::string_view layout_axis_y = "y";
constexpr std::string_view layout_index_marker = "-";
constexpr std::string_view layout_pane_legacy_console = "console";
// Protocol field counts for file format.
constexpr int layout_pane_field_count = 2;
constexpr int layout_leaf_field_min = 4;
constexpr int layout_node_field_min = 2;
constexpr int layout_split_field_count = 7;
// Split node line field indices: type, parent, axis, ratio, first child, second child.
constexpr size_t layout_split_field_type = 1;
constexpr size_t layout_split_field_parent = 2;
constexpr size_t layout_split_field_axis = 3;
constexpr size_t layout_split_field_ratio = 4;
constexpr size_t layout_split_field_first = 5;
constexpr size_t layout_split_field_second = 6;
// Buffer size for formatting split ratio with %.4f
constexpr int layout_ratio_format_buf_size = 16;
// Ratio range bounds: valid split division is from 0% to 100%
constexpr float layout_ratio_min = 0.0f;
constexpr float layout_ratio_max = 1.0f;

std::string_view kind_name(rv_editor_pane_kind k);
int kind_from_name(std::string_view s, rv_editor_pane_kind &k);
int axis_from_name(std::string_view s, rv_editor_axis &a);
int parse_u32(std::string_view s, uint32_t &out);
int parse_f32(std::string_view s, float &out);
int parse_index(std::string_view s, uint32_t &out);
std::vector<std::string_view> split_line(std::string_view line);
std::vector<std::string_view> split_text(std::string_view text);

} // namespace layout_format

} // namespace rv_editor
