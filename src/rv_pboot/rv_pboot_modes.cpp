#include "rv_pboot_modes.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/detail/rv_manifest_failer.hpp"
#include "pdklib/rv_manifest/detail/rv_manifest_lexer.hpp"
#include "pdklib/rv_manifest/detail/rv_manifest_parser.hpp"

namespace rv_3dmppc
{

namespace
{

struct rv_pboot_preset {
    const char *name;
    rv_pcslots slots;
};

// Built-in preset table, compiled in and mandatory: the console always has
// at least "sdl3" to fall back to, even before any preset FILE exists.
constexpr rv_pboot_preset kBuiltinPresets[] = { { "sdl3", rv_pcslots{} } };

// The runtime table a run actually resolves against: the built-in table,
// with a preset FILE's `[mode.NAME]` sections merged in on top (same name
// replaces, new name is added).
struct rv_pboot_runtime_preset {
    std::string name;
    rv_pcslots slots;
};

// One row per rv_pcca_impl value.
constexpr struct { const char *name; rv_pcca_impl impl; } kPcca[] = {
    { "null", rv_pcca_impl::null },
    { "sdl3", rv_pcca_impl::sdl3 },
};
constexpr struct { const char *name; rv_pccv_impl impl; } kPccv[] = {
    { "null", rv_pccv_impl::null },
    { "sdl3", rv_pccv_impl::sdl3 },
};
constexpr struct { const char *name; rv_pccio_impl impl; } kPccio[] = {
    { "null", rv_pccio_impl::null },
    { "sdl3", rv_pccio_impl::sdl3 },
};
constexpr struct { const char *name; rv_pccl_impl impl; } kPccl[] = {
    { "null", rv_pccl_impl::null },
    { "luajit", rv_pccl_impl::luajit },
};

template <typename Table, typename Impl>
const char *impl_name(const Table &table, Impl impl)
{
    for (const auto &row : table) {
        if (row.impl == impl) {
            return row.name;
        }
    }
    return "null";
}

bool find_preset(const std::vector<rv_pboot_runtime_preset> &table, const std::string &name, rv_pcslots &out)
{
    for (const rv_pboot_runtime_preset &preset : table) {
        if (name == preset.name) {
            out = preset.slots;
            return true;
        }
    }
    return false;
}

// Applies a non-empty `--mode_<slot>=value` override. Returns false (with a
// diagnostic already printed) when `value` names nothing in `table`.
template <typename Table, typename Impl>
bool apply_override(const Table &table, const std::string &slot_flag, const std::string &value,
    Impl &out, int &exit_code)
{
    if (value.empty()) {
        return true;
    }
    std::string available;
    for (const auto &row : table) {
        if (value == row.name) {
            out = row.impl;
            return true;
        }
        if (!available.empty()) {
            available += ", ";
        }
        available += row.name;
    }
    rv_console_print_error(std::format("unknown --mode_{} '{}', available: {}", slot_flag,
        rv_pdklib::rv_log_escape(value.c_str()), available));
    rv_console_print_usage(stderr);
    exit_code = 2;
    return false;
}

// Reads a `[mode.<NAME>]` value for one slot: must be a string naming a row
// of `table`. A diagnostic on `failer` and false otherwise.
template <typename Table, typename Impl>
bool lookup_slot_value(const Table &table, const rv_pdklib::rv_manifest_tree_entry &entry, Impl &out,
    rv_pdklib::rv_manifest_failer &failer)
{
    if (entry.value.kind != rv_pdklib::rv_manifest_value_kind::string) {
        failer.fail(entry.value.line,
            std::format("'{}' must be {}, got {}", entry.key, rv_pdklib::rv_manifest_kind_name(
                                                                    rv_pdklib::rv_manifest_value_kind::string),
                rv_pdklib::rv_manifest_kind_name(entry.value.kind)));
        return false;
    }
    std::string available;
    for (const auto &row : table) {
        if (entry.value.str == row.name) {
            out = row.impl;
            return true;
        }
        if (!available.empty()) {
            available += ", ";
        }
        available += row.name;
    }
    failer.fail(entry.value.line,
        std::format("unknown {} '{}', available: {}", entry.key, entry.value.str, available));
    return false;
}

// Validates the whole tree against the mode-file schema and, only if the
// file has no error, merges its presets into `table`. Returns how many
// presets were loaded; the count is meaningless when `failer` is non-empty,
// because nothing was merged.
int apply_modes_tree(const rv_pdklib::rv_manifest_tree &tree, std::vector<rv_pboot_runtime_preset> &table,
    rv_pdklib::rv_manifest_failer &failer)
{
    struct pending_preset {
        std::string name;
        int line;
        rv_pcslots slots;
    };
    std::vector<pending_preset> pendings;

    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        if (section.poisoned) {
            continue;
        }
        if (!section.name.starts_with("mode.") || section.name.size() == 5) {
            failer.fail(section.line,
                std::format("section '[{}]' is not a preset — expected '[mode.<NAME>]'", section.name));
            continue;
        }
        const std::string name = section.name.substr(5);

        bool duplicate = false;
        for (const pending_preset &p : pendings) {
            if (p.name == name) {
                failer.fail(section.line,
                    std::format("preset '{}' redefines the one at line {}", name, p.line));
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        rv_pcslots slots{};
        std::vector<std::string> seen_keys;
        for (const rv_pdklib::rv_manifest_tree_entry &entry : section.entries) {
            if (std::find(seen_keys.begin(), seen_keys.end(), entry.key) != seen_keys.end()) {
                failer.fail(entry.line, std::format("key '{}' given twice", entry.key));
                continue;
            }
            seen_keys.push_back(entry.key);

            if (entry.key == "ca") {
                lookup_slot_value(kPcca, entry, slots.ca, failer);
            } else if (entry.key == "cv") {
                lookup_slot_value(kPccv, entry, slots.cv, failer);
            } else if (entry.key == "cio") {
                lookup_slot_value(kPccio, entry, slots.cio, failer);
            } else if (entry.key == "cl") {
                lookup_slot_value(kPccl, entry, slots.cl, failer);
            } else {
                failer.fail(entry.line, std::format("unknown key '{}'", entry.key));
            }
        }

        pendings.push_back({ name, section.line, slots });
    }

    if (!failer.empty()) {
        return 0;
    }

    for (const pending_preset &p : pendings) {
        bool replaced = false;
        for (rv_pboot_runtime_preset &existing : table) {
            if (existing.name == p.name) {
                existing.slots = p.slots;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            table.push_back({ p.name, p.slots });
        }
    }
    return static_cast<int>(pendings.size());
}

// Merges `build/modes.toml` (next to the running executable) into `table`.
// Absent file: nothing happens. Present but unreadable, or present with a
// schema/parse problem: a diagnostic and false. `table` is left untouched on
// failure.
bool load_modes_file(std::vector<rv_pboot_runtime_preset> &table, int &exit_code)
{
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return true;
    }
    const std::filesystem::path path = exe.parent_path() / "modes.toml";

    std::error_code exists_ec;
    if (!std::filesystem::exists(path, exists_ec)) {
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        rv_console_print_error(std::format("cannot open modes file '{}'", path.string()));
        exit_code = 2;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        rv_console_print_error(std::format("cannot read modes file '{}'", path.string()));
        exit_code = 2;
        return false;
    }
    const std::string text = buffer.str();

    rv_pdklib::rv_manifest_failer failer;
    const std::vector<rv_pdklib::rv_manifest_token> tokens = rv_pdklib::rv_manifest_lex(text);
    rv_pdklib::rv_manifest_parser parser(tokens, failer);
    const rv_pdklib::rv_manifest_tree tree = parser.run();

    int loaded = 0;
    if (failer.empty()) {
        loaded = apply_modes_tree(tree, table, failer);
    }

    if (!failer.empty()) {
        const std::string report = failer.report(path.string());
        std::size_t start = 0;
        while (start <= report.size()) {
            const std::size_t nl = report.find('\n', start);
            rv_console_print_error(report.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
        exit_code = 2;
        return false;
    }

    RV_LOG_INFO("main", "modes: {} preset(s) from '{}'", loaded, path.string());
    return true;
}

} // namespace

bool rv_pboot_modes_resolve(const rv_pboot_args &args, rv_pcslots &out, int &exit_code)
{
    std::vector<rv_pboot_runtime_preset> table;
    for (const rv_pboot_preset &preset : kBuiltinPresets) {
        table.push_back({ preset.name, preset.slots });
    }

    if (!load_modes_file(table, exit_code)) {
        return false;
    }

    rv_pcslots slots;
    if (!find_preset(table, args.mode, slots)) {
        std::string available;
        for (const rv_pboot_runtime_preset &preset : table) {
            if (!available.empty()) {
                available += ", ";
            }
            available += preset.name;
        }
        rv_console_print_error(
            std::format("unknown --mode '{}', available: {}", rv_pdklib::rv_log_escape(args.mode.c_str()),
                available));
        rv_console_print_usage(stderr);
        exit_code = 2;
        return false;
    }

    if (!apply_override(kPcca, "ca", args.mode_ca, slots.ca, exit_code) ||
        !apply_override(kPccv, "cv", args.mode_cv, slots.cv, exit_code) ||
        !apply_override(kPccio, "cio", args.mode_cio, slots.cio, exit_code) ||
        !apply_override(kPccl, "cl", args.mode_cl, slots.cl, exit_code)) {
        return false;
    }

    out = slots;
    return true;
}

const char *rv_pboot_impl_name(rv_pcca_impl impl)
{
    return impl_name(kPcca, impl);
}
const char *rv_pboot_impl_name(rv_pccv_impl impl)
{
    return impl_name(kPccv, impl);
}
const char *rv_pboot_impl_name(rv_pccio_impl impl)
{
    return impl_name(kPccio, impl);
}
const char *rv_pboot_impl_name(rv_pccl_impl impl)
{
    return impl_name(kPccl, impl);
}

} // namespace rv_3dmppc
