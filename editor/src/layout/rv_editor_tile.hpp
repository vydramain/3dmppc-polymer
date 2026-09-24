#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

// The tile tree of the workspace (docs/adr/0002-tiling.md): data and pure
// operations, no ImGui. Panes are views; closing a tile closes no model behind it.

// The direction a split divides in. x: side by side, the bar moves along X.
enum class rv_editor_axis
{
    x,
    y,
};

struct rv_editor_size
{
    int32_t w;
    int32_t h;
};

struct rv_editor_rect
{
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
};

// --- panes --------------------------------------------------------------------

using rv_editor_pane_id = uint32_t;

// What a pane shows (requirements, 5.1). The kind can change; the id stays.
enum class rv_editor_pane_kind : uint32_t
{
    empty,
    catalog,
    project,
    files,
    assets,
    scene,
    hierarchy,
    inspector,
    game,
    code,
    controls,
    run_config,
    output,
    terminal,
    problems,
    search,
    toolchest,
    console,
};

struct rv_editor_pane
{
    rv_editor_pane_kind kind;
};

// Pane id -> pane. Ids are indices and are never reused, so a saved layout keeps
// pointing at the same pane. A pane is shown while the tree holds its id.
struct rv_editor_pane_registry
{
    std::vector<rv_editor_pane> panes;
};

rv_editor_pane_id rv_editor_pane_add(rv_editor_pane_registry &registry, rv_editor_pane_kind kind);
bool rv_editor_pane_set_kind(rv_editor_pane_registry &registry, rv_editor_pane_id id, rv_editor_pane_kind kind);

// --- the tree -----------------------------------------------------------------

inline constexpr uint32_t rv_editor_tile_none = UINT32_MAX;

struct rv_editor_tile_split
{
    rv_editor_axis axis;
    float ratio;     // share of the first child, 0..1; minimums win over it
    uint32_t first;  // left or top
    uint32_t second; // right or bottom
};

struct rv_editor_tile_leaf
{
    std::vector<rv_editor_pane_id> tabs;
    uint32_t active; // index into tabs
};

enum class rv_editor_tile_kind
{
    free, // an unused slot of the node pool
    split,
    leaf,
};

struct rv_editor_tile_node
{
    rv_editor_tile_kind kind;
    uint32_t parent; // rv_editor_tile_none for the root
    rv_editor_tile_split split;
    rv_editor_tile_leaf leaf;
};

// Nodes are a pool addressed by index. An operation keeps the index of every
// node it does not remove, so a caller may hold a leaf index across operations.
struct rv_editor_layout
{
    std::vector<rv_editor_tile_node> nodes;
    uint32_t root;
    uint32_t maximized_leaf; // rv_editor_tile_none when nothing is maximized
};

// Where a pane lands relative to a target leaf.
enum class rv_editor_tile_dock
{
    tab, // into the leaf's tab strip
    left,
    right,
    top,
    bottom,
};

// One leaf holding `pane`, or an empty leaf for rv_editor_tile_none.
rv_editor_layout rv_editor_layout_make(rv_editor_pane_id pane);

// The leaf that holds `pane`, or rv_editor_tile_none.
uint32_t rv_editor_tile_find(const rv_editor_layout &layout, rv_editor_pane_id pane);

// Put `pane` into or beside `leaf`. Returns the leaf that now holds it, or
// rv_editor_tile_none when `leaf` is not a leaf. The pane must not be in the tree.
uint32_t rv_editor_tile_insert(rv_editor_layout &layout, uint32_t leaf, rv_editor_pane_id pane,
    rv_editor_tile_dock dock);

// Take `pane` out of the tree. A leaf left without tabs is removed and its parent
// split gives its place to the sibling; the last leaf stays, empty.
bool rv_editor_tile_remove(rv_editor_layout &layout, rv_editor_pane_id pane);

// Move `pane` into or beside `leaf`. False, changing nothing, when the move is
// impossible: a leaf split or tabbed by its own only pane, or an unknown pane or leaf.
bool rv_editor_tile_move(rv_editor_layout &layout, rv_editor_pane_id pane, uint32_t leaf, rv_editor_tile_dock dock);

bool rv_editor_tile_activate(rv_editor_layout &layout, rv_editor_pane_id pane);
bool rv_editor_tile_set_ratio(rv_editor_layout &layout, uint32_t split, float ratio);

// Maximize `leaf`, or restore the tree when it is already the maximized one.
bool rv_editor_tile_toggle_maximize(rv_editor_layout &layout, uint32_t leaf);

// --- placement ----------------------------------------------------------------

// What the view adds around the panes: the splitter bar between two children and
// a leaf's own chrome (header, tab strip, frame) on top of its pane's minimum.
struct rv_editor_tile_metrics
{
    int32_t bar;
    rv_editor_size chrome;
};

// Where one node lands. `overflow` marks a split whose children's minimums do not
// fit its rectangle: the children are shrunk in proportion to their minimums and
// the view offers tabs or Fit instead (LAY-04).
struct rv_editor_tile_place
{
    uint32_t node;
    rv_editor_rect rect;
    bool overflow;
};

// Smallest rectangle `node` can take. `pane_min` is indexed by pane id; a pane
// without an entry has no minimum of its own.
rv_editor_size rv_editor_tile_min_size(const rv_editor_layout &layout, uint32_t node, const rv_editor_tile_metrics &metrics,
    const std::vector<rv_editor_size> &pane_min);

// Rectangles of every node shown in `area`, parents before children; only the
// maximized leaf when one is maximized.
std::vector<rv_editor_tile_place> rv_editor_layout_place(const rv_editor_layout &layout, rv_editor_rect area,
    const rv_editor_tile_metrics &metrics, const std::vector<rv_editor_size> &pane_min);

// --- text form ----------------------------------------------------------------

// The saved form of a workspace: the pane registry and the tree, one record per
// line, first line "3dmppc-editor-layout 1".
std::string rv_editor_layout_write(const rv_editor_pane_registry &panes, const rv_editor_layout &layout);

// Reads what rv_editor_layout_write wrote. False, with both outputs untouched,
// unless `text` is one whole valid layout.
bool rv_editor_layout_read(std::string_view text, rv_editor_pane_registry &panes, rv_editor_layout &layout);

// --- starting layouts ---------------------------------------------------------

enum class rv_editor_layout_preset
{
    code,
    scene,
    debug,
    build,
};

// Name as a menu shows it: "Code", "Scene", "Debug", "Build".
const char *rv_editor_layout_preset_name(rv_editor_layout_preset preset);

// Replaces both outputs with the starting layout `preset` (LAY-07).
void rv_editor_layout_preset_make(rv_editor_layout_preset preset, rv_editor_pane_registry &panes,
    rv_editor_layout &layout);

// --- the layout file ----------------------------------------------------------

// $XDG_CONFIG_HOME/3dmppc-editor/layout, or $HOME/.config/3dmppc-editor/layout when XDG_CONFIG_HOME is unset, empty
// or not absolute. Empty when neither gives an absolute directory.
std::filesystem::path rv_editor_layout_file_path();

// False, with both outputs untouched, when the file is missing, larger than 1 MiB or not one whole valid layout.
bool rv_editor_layout_load(const std::filesystem::path &path, rv_editor_pane_registry &panes, rv_editor_layout &layout);

// Creates the directory, writes "<path>.tmp" and renames it over `path`, so a crash never leaves half a file.
// False with the reason in `error` when any step fails; the old file then stays as it was.
bool rv_editor_layout_save(const std::filesystem::path &path, const rv_editor_pane_registry &panes,
    const rv_editor_layout &layout, std::string &error);

} // namespace rv_editor
