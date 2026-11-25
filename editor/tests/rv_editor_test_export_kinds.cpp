// The export kinds table, the path each kind remembers, and the rule that an export replaces only
// an empty directory or one a previous export made.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

#include "build/rv_editor_export.hpp"
#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

namespace
{

namespace fs = std::filesystem;

constexpr rv_editor_export_kind all_kinds[] = { rv_editor_export_kind::disc,
    rv_editor_export_kind::player,
    rv_editor_export_kind::web };

// A fresh scratch directory under the system temp directory; the caller removes it.
fs::path scratch(const char *name)
{
    const fs::path dir = fs::temp_directory_path() / (std::string("rv_editor_test_") + name + "_" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void touch(const fs::path &file)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream(file).put('x');
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_export_kinds_table_rows)
{
    const auto rows = rv_editor_export_kinds();
    RV_EDITOR_CHECK(rows.size() == 3);
    std::set<std::string_view> subdirs;
    for (size_t i = 0; i < rows.size(); ++i) {
        RV_EDITOR_CHECK(static_cast<size_t>(rows[i].kind) == i); // row at its kind's index
        RV_EDITOR_CHECK(!rows[i].subdir.empty());
        subdirs.insert(rows[i].subdir);
        // A text key that is not in the texts file comes back as itself.
        RV_EDITOR_CHECK(!rows[i].name_key.empty());
        RV_EDITOR_CHECK(std::string_view(rv_editor_text(rows[i].name_key)) != rows[i].name_key);
        RV_EDITOR_CHECK(!rows[i].description_key.empty());
        RV_EDITOR_CHECK(std::string_view(rv_editor_text(rows[i].description_key)) != rows[i].description_key);
    }
    RV_EDITOR_CHECK(subdirs.size() == rows.size()); // unique
}

RV_EDITOR_TEST(rv_editor_test_export_kinds_table_values)
{
    const auto rows = rv_editor_export_kinds();
    if (rows.size() != 3) {
        RV_EDITOR_CHECK(rows.size() == 3);
        return;
    }
    RV_EDITOR_CHECK(rows[0].kind == rv_editor_export_kind::disc && rows[0].subdir == "mppcdisc");
    RV_EDITOR_CHECK(rows[1].kind == rv_editor_export_kind::player && rows[1].subdir == "bundle");
    RV_EDITOR_CHECK(rows[2].kind == rv_editor_export_kind::web && rows[2].subdir == "web");
    RV_EDITOR_CHECK(!rows[0].needs_web_tools && !rows[1].needs_web_tools && rows[2].needs_web_tools);
    RV_EDITOR_CHECK(rows[0].open == rv_editor_export_open_action::folder);
    RV_EDITOR_CHECK(rows[1].open == rv_editor_export_open_action::folder);
    RV_EDITOR_CHECK(rows[2].open == rv_editor_export_open_action::browser);
    RV_EDITOR_CHECK(std::string_view(rv_editor_text(rows[1].name_key)) == "Bundle"); // the player kind is named Bundle
}

RV_EDITOR_TEST(rv_editor_test_export_default_dirs)
{
    const fs::path root = "/work/proj";
    RV_EDITOR_CHECK(rv_editor_export_default_dir(root, rv_editor_export_kind::disc) == root / "export" / "mppcdisc");
    RV_EDITOR_CHECK(rv_editor_export_default_dir(root, rv_editor_export_kind::player) == root / "export" / "bundle");
    RV_EDITOR_CHECK(rv_editor_export_default_dir(root, rv_editor_export_kind::web) == root / "export" / "web");
}

RV_EDITOR_TEST(rv_editor_test_export_paths_remembered_per_kind)
{
    const fs::path root = "/work/proj";
    rv_editor_export_paths paths;
    // Never edited: the default directory.
    for (const auto kind : all_kinds) {
        RV_EDITOR_CHECK(paths.get(root, kind) == rv_editor_export_default_dir(root, kind).string());
    }
    // Edit Disc, look at the others, come back: Disc keeps its path, the others are untouched.
    paths.set(rv_editor_export_kind::disc, "/elsewhere/discs");
    RV_EDITOR_CHECK(paths.get(root, rv_editor_export_kind::disc) == "/elsewhere/discs");
    RV_EDITOR_CHECK(paths.get(root, rv_editor_export_kind::web) == (root / "export" / "web").string());
    RV_EDITOR_CHECK(paths.get(root, rv_editor_export_kind::player) == (root / "export" / "bundle").string());
    paths.set(rv_editor_export_kind::web, "rel/web");
    RV_EDITOR_CHECK(paths.get(root, rv_editor_export_kind::web) == "rel/web");
    RV_EDITOR_CHECK(paths.get(root, rv_editor_export_kind::disc) == "/elsewhere/discs");
    // The project closes: all back to the defaults.
    paths.reset();
    for (const auto kind : all_kinds) {
        RV_EDITOR_CHECK(paths.get(root, kind) == rv_editor_export_default_dir(root, kind).string());
    }
}

RV_EDITOR_TEST(rv_editor_test_export_prepare_missing_is_created)
{
    const fs::path base = scratch("prep_missing");
    const fs::path dir = base / "a" / "b";
    std::string error;
    RV_EDITOR_CHECK(rv_editor_export_prepare_dir(dir, error) == RV_OK);
    RV_EDITOR_CHECK(fs::is_directory(dir));
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_prepare_empty_is_ok)
{
    const fs::path base = scratch("prep_empty");
    std::string error;
    RV_EDITOR_CHECK(rv_editor_export_prepare_dir(base, error) == RV_OK);
    RV_EDITOR_CHECK(fs::is_directory(base));
    RV_EDITOR_CHECK(fs::is_empty(base));
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_prepare_marked_is_emptied)
{
    const fs::path base = scratch("prep_marked");
    touch(base / "old.txt");
    touch(base / "sub" / "deep.txt");
    touch(base / std::string(rv_editor_export_mark_name));
    std::string error;
    RV_EDITOR_CHECK(rv_editor_export_prepare_dir(base, error) == RV_OK);
    RV_EDITOR_CHECK(fs::is_directory(base));
    RV_EDITOR_CHECK(!fs::exists(base / "old.txt"));
    RV_EDITOR_CHECK(!fs::exists(base / "sub"));
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_prepare_unmarked_is_refused_and_untouched)
{
    const fs::path base = scratch("prep_unmarked");
    touch(base / "mine.txt");
    touch(base / "sub" / "deep.txt");
    std::string error;
    RV_EDITOR_CHECK(rv_editor_export_prepare_dir(base, error) != RV_OK);
    RV_EDITOR_CHECK(!error.empty());
    RV_EDITOR_CHECK(fs::exists(base / "mine.txt"));
    RV_EDITOR_CHECK(fs::exists(base / "sub" / "deep.txt"));
    std::error_code ec;
    fs::remove_all(base, ec);
}

namespace
{

// A burner stand-in: `build <root> --target <t> -o <out> ...` writes new.txt into the output directory.
fs::path fake_burner(const fs::path &where)
{
    const fs::path script = where / "burner.sh";
    std::ofstream(script) << "#!/bin/sh\nmkdir -p \"$6\" && echo new > \"$6/new.txt\"\n";
    chmod(script.c_str(), 0700);
    return script;
}

// Starts a web export of a stand-in project into `dest` and waits for it to end; returns start()'s code.
int run_export(const fs::path &root,
    const fs::path &burner,
    const fs::path &dest,
    rv_editor_export &exporter,
    std::string &error)
{
    // The process guard starts once per process; another test may have started it, so the result is not checked.
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    rv_editor_project project;
    project.open = true;
    project.root = root;
    project.disc_id = "disc";
    project.cache_dir = root / "cache";
    rv_editor_toolchain tools;
    tools.burner.path = burner;
    tools.baker.path = burner;
    rv_editor_log log;
    const int rc = exporter.start(project, tools, rv_editor_export_kind::web, dest, log, error);
    for (int i = 0; rc == RV_OK && i < 500 && exporter.busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string update_error;
        exporter.update(log, update_error);
    }
    return rc;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_export_success_writes_mark_and_replaces_marked)
{
    const fs::path base = scratch("export_marked");
    const fs::path dest = base / "out";
    touch(dest / "old.txt");
    touch(dest / std::string(rv_editor_export_mark_name));
    rv_editor_export exporter;
    std::string error;
    RV_EDITOR_CHECK(run_export(base, fake_burner(base), dest, exporter, error) == RV_OK);
    RV_EDITOR_CHECK(exporter.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(!fs::exists(dest / "old.txt"));
    RV_EDITOR_CHECK(fs::exists(dest / "new.txt"));
    RV_EDITOR_CHECK(fs::exists(dest / std::string(rv_editor_export_mark_name)));
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_success_into_missing_dir_writes_mark)
{
    const fs::path base = scratch("export_missing");
    const fs::path dest = base / "fresh";
    rv_editor_export exporter;
    std::string error;
    RV_EDITOR_CHECK(run_export(base, fake_burner(base), dest, exporter, error) == RV_OK);
    RV_EDITOR_CHECK(exporter.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(fs::exists(dest / "new.txt"));
    RV_EDITOR_CHECK(fs::exists(dest / std::string(rv_editor_export_mark_name)));
    std::error_code ec;
    fs::remove_all(base, ec);
}

namespace
{

// A burner stand-in that runs `body` after creating the output directory ($6).
fs::path script_burner(const fs::path &where, const char *name, const char *body)
{
    const fs::path script = where / name;
    std::ofstream(script) << "#!/bin/sh\nmkdir -p \"$6\" && " << body << "\n";
    chmod(script.c_str(), 0700);
    return script;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_export_failed_export_leaves_dir_ours_for_the_next_export)
{
    const fs::path base = scratch("export_failed_then_again");
    const fs::path dest = base / "fresh";
    const fs::path failing = script_burner(base, "failing.sh", "echo half > \"$6/half.txt\"; exit 1");
    rv_editor_export first;
    std::string error;
    RV_EDITOR_CHECK(run_export(base, failing, dest, first, error) == RV_OK);
    RV_EDITOR_CHECK(first.state() == rv_editor_build_state::failed);
    RV_EDITOR_CHECK(fs::exists(dest / "half.txt"));

    rv_editor_export second;
    error.clear();
    RV_EDITOR_CHECK(run_export(base, fake_burner(base), dest, second, error) == RV_OK);
    RV_EDITOR_CHECK(second.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(!fs::exists(dest / "half.txt"));
    RV_EDITOR_CHECK(fs::exists(dest / "new.txt"));
    RV_EDITOR_CHECK(fs::exists(dest / std::string(rv_editor_export_mark_name)));
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(dest)) {
        names.insert(entry.path().filename().string());
    }
    RV_EDITOR_CHECK((names == std::set<std::string>{ "new.txt", std::string(rv_editor_export_mark_name) }));
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_dir_is_marked_as_soon_as_start_returns)
{
    const fs::path base = scratch("export_marked_at_start");
    const fs::path dest = base / "fresh";
    const fs::path slow = script_burner(base, "slow.sh", "sleep 1; echo new > \"$6/new.txt\"");
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    rv_editor_project project;
    project.open = true;
    project.root = base;
    project.disc_id = "disc";
    project.cache_dir = base / "cache";
    rv_editor_toolchain tools;
    tools.burner.path = slow;
    tools.baker.path = slow;
    rv_editor_log log;
    rv_editor_export exporter;
    std::string error;
    RV_EDITOR_CHECK(exporter.start(project, tools, rv_editor_export_kind::web, dest, log, error) == RV_OK);
    RV_EDITOR_CHECK(exporter.busy());
    RV_EDITOR_CHECK(fs::exists(dest / std::string(rv_editor_export_mark_name)));
    for (int i = 0; i < 500 && exporter.busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string update_error;
        exporter.update(log, update_error);
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

RV_EDITOR_TEST(rv_editor_test_export_start_refuses_unmarked_non_empty_dir)
{
    const fs::path base = scratch("export_unmarked");
    const fs::path dest = base / "docs";
    touch(dest / "mine.txt");
    rv_editor_export exporter;
    std::string error;
    RV_EDITOR_CHECK(run_export(base, fake_burner(base), dest, exporter, error) != RV_OK);
    RV_EDITOR_CHECK(!error.empty());
    RV_EDITOR_CHECK(!exporter.busy());
    RV_EDITOR_CHECK(fs::exists(dest / "mine.txt"));
    RV_EDITOR_CHECK(!fs::exists(dest / "new.txt")); // the burner never ran
    RV_EDITOR_CHECK(!fs::exists(dest / std::string(rv_editor_export_mark_name)));
    std::error_code ec;
    fs::remove_all(base, ec);
}

namespace
{

// Exports with `dest_is_parent` (the project's parent directory) or the project root itself as the destination,
// with or without the export mark; the export is refused and every file stays.
void check_export_refused_for_project_dest(const char *name, bool dest_is_parent, bool marked)
{
    const fs::path base = scratch(name);
    const fs::path parent = base / "parent";
    const fs::path root = parent / "proj";
    touch(root / "project.txt");
    touch(root / "sub" / "deep.txt");
    touch(parent / "sibling.txt");
    const fs::path dest = dest_is_parent ? parent : root;
    if (marked) {
        touch(dest / std::string(rv_editor_export_mark_name));
    }
    const fs::path burner = fake_burner(base);
    rv_editor_export exporter;
    std::string error;
    RV_EDITOR_CHECK(run_export(root, burner, dest, exporter, error) != RV_OK);
    RV_EDITOR_CHECK(!error.empty());
    RV_EDITOR_CHECK(!exporter.busy());
    RV_EDITOR_CHECK(fs::exists(root / "project.txt"));
    RV_EDITOR_CHECK(fs::exists(root / "sub" / "deep.txt"));
    RV_EDITOR_CHECK(fs::exists(parent / "sibling.txt"));
    RV_EDITOR_CHECK(!fs::exists(dest / "new.txt")); // the burner never ran
    std::error_code ec;
    fs::remove_all(base, ec);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_export_start_refuses_project_root_as_destination)
{
    check_export_refused_for_project_dest("export_dest_root", false, false);
}

RV_EDITOR_TEST(rv_editor_test_export_start_refuses_project_root_as_marked_destination)
{
    check_export_refused_for_project_dest("export_dest_root_marked", false, true);
}

RV_EDITOR_TEST(rv_editor_test_export_start_refuses_project_parent_as_destination)
{
    check_export_refused_for_project_dest("export_dest_parent", true, false);
}

RV_EDITOR_TEST(rv_editor_test_export_start_refuses_project_parent_as_marked_destination)
{
    check_export_refused_for_project_dest("export_dest_parent_marked", true, true);
}
