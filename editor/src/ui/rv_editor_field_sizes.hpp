#pragma once

#include <cstddef>

namespace rv_editor
{

// ImGui text field buffer sizes, named by semantic meaning.

// Short text for names and filters: run config name, asset filter
constexpr size_t short_text_field_size = 64;

// One-line search/filter query field: Output pane search
constexpr size_t search_field_size = 128;

// Object name identifier: scene object names
constexpr size_t identifier_field_size = 128;

// Mark moment note: debug session annotations
constexpr size_t mark_note_field_size = 128;

// One-line title: finding title
constexpr size_t title_field_size = 160;

// Asset name and file identifier: mesh, texture, file names
constexpr size_t asset_identifier_field_size = 256;

// Filesystem path: runtime, memory card, working directory
constexpr size_t filesystem_path_field_size = 512;

// Finding or test note: additional note text
constexpr size_t note_field_size = 512;

// Command-line arguments or environment: args, env
constexpr size_t command_line_field_size = 1024;

// Test report text: expected and actual results
constexpr size_t report_field_size = 1024;

// Multi-line test procedure: detailed steps
constexpr size_t test_steps_field_size = 2048;

} // namespace rv_editor
