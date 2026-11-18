// The web setup's bar is due from the first frame (the line parsing is in rv_editor_test_progress.cpp).

#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "build/rv_editor_export.hpp"
#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_project.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_setup_bar_due_from_first_frame)
{
    // A burner stand-in that outlives the check; `setup web` are its ignored arguments.
    char name[] = "/tmp/rv_editor_test_burner_XXXXXX";
    const int fd = mkstemp(name);
    RV_EDITOR_CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }
    const char script[] = "#!/bin/sh\nsleep 30\n";
    RV_EDITOR_CHECK(write(fd, script, sizeof(script) - 1) == static_cast<ssize_t>(sizeof(script) - 1));
    close(fd);
    chmod(name, 0700);

    std::string guard_error;
    // as main() does before any job; another test file may have started it already
    const int guard_rc = rv_editor_guard_start(guard_error);
    RV_EDITOR_CHECK(guard_rc == RV_OK || guard_rc == RV_ERR_BUSY);

    rv_editor_toolchain tools;
    tools.burner.path = name;
    rv_editor_log log;
    rv_editor_export exporter;
    RV_EDITOR_CHECK(!rv_editor_export_setup_bar_due(exporter)); // nothing runs yet
    std::string error;
    const int started = exporter.start_setup(tools, log, error);
    if (started != RV_OK) {
        std::fprintf(stderr, "start_setup: %s\n", error.c_str());
    }
    RV_EDITOR_CHECK(started == RV_OK);
    RV_EDITOR_CHECK(exporter.busy());
    RV_EDITOR_CHECK(rv_editor_export_setup_bar_due(exporter));

    exporter.cancel(error);
    for (int i = 0; i < 200 && exporter.busy(); ++i) {
        usleep(20000);
        exporter.update(log, error);
    }
    std::filesystem::remove(name);
}
