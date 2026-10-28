#pragma once

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_inspect/rv_inspect_read.hpp"
#include "pdklib/rv_repair/rv_repair_file.hpp"

// Repair core: named rules over file texts, applied to the findings of one inspection run.
// Every file is read and repaired in memory first; a file is written only after that.

namespace rv_pdklib
{

// Changes `text` in place for the findings of one file that name this rule. Their `file` is that
// file's canonical path relative to the canonical disc, in generic form, not the inspection spelling.
using rv_repair_fn = int (*)(std::string &text, std::span<const rv_inspect_finding> findings, std::string &error);

struct rv_repair_rule {
    std::string_view name;
    rv_repair_fn apply;
};

// A written file and the copy taken before the write.
struct rv_repair_written {
    std::filesystem::path rel;
    std::filesystem::path copy;
};

namespace detail
{

// Text of one file before and after its rules.
struct rv_repair_change {
    std::string rel;
    std::string before;
    std::string after;
};

inline const rv_repair_rule *rv_repair_find(std::span<const rv_repair_rule> rules, std::string_view name)
{
    for (const rv_repair_rule &rule : rules) {
        if (rule.name == name) {
            return &rule;
        }
    }
    return nullptr;
}

// Findings of one file that name the rule `name`, in report order.
inline std::vector<rv_inspect_finding> rv_repair_for_rule(std::span<const rv_inspect_finding> findings, std::string_view name)
{
    std::vector<rv_inspect_finding> out;
    for (const rv_inspect_finding &f : findings) {
        if (f.fix == name) {
            out.push_back(f);
        }
    }
    return out;
}

// A rule name in the table twice would run both entries on one set of findings.
inline int rv_repair_check_table(std::span<const rv_repair_rule> rules, std::string &error)
{
    for (size_t i = 0; i < rules.size(); ++i) {
        for (size_t j = i + 1; j < rules.size(); ++j) {
            if (rules[i].name == rules[j].name) {
                error = "repair: rule name '" + std::string(rules[i].name) + "' is in the table twice";
                return RV_ERR_INVAL;
            }
        }
    }
    return RV_OK;
}

// A missing path is RV_ERR_NOENT, as in the backup and the write; any other failure is I/O.
inline int rv_repair_resolve_fail(const std::error_code &ec, const std::string &what, std::string &error)
{
    const bool missing = ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory;
    error = "repair: cannot resolve " + what + ": " + ec.message();
    return missing ? RV_ERR_NOENT : RV_ERR_IO;
}

// Resolves each repairable finding's file through links. `keys[i]` gets the resolved path relative
// to the resolved disc: findings that reach one file share a key. The file must lie under the disc,
// so a symlink cannot lead a write outside it. Skipped findings get an empty key.
inline int rv_repair_resolve(const std::filesystem::path &disc,
    std::span<const rv_inspect_finding> findings,
    std::vector<std::string> &keys,
    std::string &error)
{
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::canonical(disc, ec);
    if (ec) {
        return rv_repair_resolve_fail(ec, disc.string(), error);
    }
    keys.assign(findings.size(), {});
    for (size_t i = 0; i < findings.size(); ++i) {
        const rv_inspect_finding &f = findings[i];
        if (f.fix.empty()) {
            continue;
        }
        const std::string where = f.file + ":" + std::to_string(f.line);
        const std::filesystem::path real = std::filesystem::canonical(disc / f.file, ec);
        if (ec) {
            return rv_repair_resolve_fail(ec, where, error);
        }
        if (real == root) {
            error = "repair: " + where + " resolves to the disc itself";
            return RV_ERR_INVAL;
        }
        const std::filesystem::path inside = real.lexically_relative(root);
        if (inside.empty() || *inside.begin() == "..") {
            error = "repair: " + where + " resolves outside the disc";
            return RV_ERR_INVAL;
        }
        // A FIFO or device would block the read; status does not open the file.
        const std::filesystem::file_status st = std::filesystem::status(real, ec);
        if (ec) {
            return rv_repair_resolve_fail(ec, where, error);
        }
        if (!std::filesystem::is_regular_file(st)) {
            error = "repair: " + where + " is not a regular file";
            return RV_ERR_INVAL;
        }
        keys[i] = inside.generic_string();
    }
    return RV_OK;
}

// Checks every repairable finding before any file is read. Findings with an empty fix are skipped.
inline int
rv_repair_check(std::span<const rv_repair_rule> rules, std::span<const rv_inspect_finding> findings, std::string &error)
{
    for (const rv_inspect_finding &f : findings) {
        if (f.fix.empty()) {
            continue;
        }
        if (rv_repair_find(rules, f.fix) == nullptr) {
            error = "repair: unknown fix '" + f.fix + "' at " + f.file + ":" + std::to_string(f.line);
            return RV_ERR_INVAL;
        }
        if (!rv_repair_rel_inside(f.file)) {
            error = "repair: " + f.file + ":" + std::to_string(f.line) + " is not a path inside the disc";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Reads each affected file in path order and applies its rules in table order, once each.
// Nothing is written here.
inline int rv_repair_stage(std::span<const rv_repair_rule> rules,
    const std::filesystem::path &disc,
    const std::map<std::string, std::vector<rv_inspect_finding>> &by_file,
    std::vector<rv_repair_change> &changes,
    std::string &error)
{
    for (const auto &[file, file_findings] : by_file) {
        rv_repair_change change{ file, {}, {} };
        if (const int rc = rv_inspect_read_text(disc / file, change.before, error); rc != RV_OK) {
            return rc;
        }
        change.after = change.before;
        for (const rv_repair_rule &rule : rules) {
            const std::vector<rv_inspect_finding> mine = rv_repair_for_rule(file_findings, rule.name);
            if (mine.empty()) {
                continue;
            }
            if (const int rc = rule.apply(change.after, mine, error); rc != RV_OK) {
                error = file + ": rule " + std::string(rule.name) + ": " + error;
                return rc;
            }
        }
        changes.push_back(std::move(change));
    }
    return RV_OK;
}

// Backup and atomic write of each changed file, in path order. A failed write keeps its
// backup, named in `error`. The first failure stops the run; `out` keeps the files written.
inline int rv_repair_commit(const std::filesystem::path &disc,
    std::span<const rv_repair_change> changes,
    std::string_view stamp,
    std::vector<rv_repair_written> &out,
    std::string &error)
{
    for (const rv_repair_change &change : changes) {
        if (change.after == change.before) {
            continue;
        }
        const std::filesystem::path rel(change.rel);
        std::filesystem::path copy;
        if (const int rc = rv_repair_backup(disc, rel, stamp, copy, error); rc != RV_OK) {
            return rc;
        }
        if (const int rc = rv_repair_write_atomic(disc / rel, change.after, error); rc != RV_OK) {
            error += "; backup kept: " + copy.string();
            return rc;
        }
        out.push_back({ rel, copy });
    }
    return RV_OK;
}

} // namespace detail

// Repairs the files of `disc` named by `findings`. `stamp` names the backup copies of the whole
// run. `out` is replaced by the written files; on error it holds those written before the failure.
inline int rv_repair_apply(std::span<const rv_repair_rule> rules,
    const std::filesystem::path &disc,
    std::span<const rv_inspect_finding> findings,
    std::string_view stamp,
    std::vector<rv_repair_written> &out,
    std::string &error)
{
    out.clear();
    if (stamp.empty()) {
        error = "repair: empty time stamp";
        return RV_ERR_INVAL;
    }
    if (const int rc = detail::rv_repair_check_table(rules, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = detail::rv_repair_check(rules, findings, error); rc != RV_OK) {
        return rc;
    }
    std::vector<std::string> keys;
    if (const int rc = detail::rv_repair_resolve(disc, findings, keys, error); rc != RV_OK) {
        return rc;
    }
    std::map<std::string, std::vector<rv_inspect_finding>> by_file;
    for (size_t i = 0; i < findings.size(); ++i) {
        if (!findings[i].fix.empty()) {
            rv_inspect_finding f = findings[i];
            f.file = keys[i];
            by_file[keys[i]].push_back(std::move(f));
        }
    }
    std::vector<detail::rv_repair_change> changes;
    if (const int rc = detail::rv_repair_stage(rules, disc, by_file, changes, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = detail::rv_repair_commit(disc, changes, stamp, out, error); rc != RV_OK) {
        return rc;
    }
    return RV_OK;
}

} // namespace rv_pdklib
