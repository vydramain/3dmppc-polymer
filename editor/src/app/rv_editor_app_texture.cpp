// Reload on a texture: bakes the changed PNG with the burner into staging, then
// sends the baked bytes over the session's reload slot. One bake at a time, async.

#include "app/rv_editor_app.hpp"

#include <fstream>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Bytes read from the baker per poll.
constexpr size_t read_chunk_bytes = 1 << 20;

// Cache subdirectory for baked textures.
constexpr std::string_view cache_staging_dir = "staging";

// Burner command to bake a texture.
constexpr std::string_view burner_cmd_bake = "bake-texture";

// Burner argument for output file path.
constexpr std::string_view burner_arg_output = "-o";

// Burner argument to specify the baker tool path.
constexpr std::string_view burner_arg_baker = "--baker";

// The last non-empty line of `text`, for a one-line failure summary.
std::string rv_editor_texture_last_line(const std::string &text)
{
    size_t end = text.find_last_not_of('\n');
    if (end == std::string::npos) {
        return {};
    }
    const size_t start = text.rfind('\n', end);
    return text.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start));
}

// Reads the whole file into bytes; RV_OK on success, RV_ERR_IO on any I/O error.
int rv_editor_texture_read(const std::filesystem::path &path, std::vector<unsigned char> &bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return RV_ERR_IO;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return RV_ERR_IO;
    }
    bytes.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char *>(bytes.data()), size);
    }
    return (static_cast<bool>(in) || in.eof()) ? RV_OK : RV_ERR_IO;
}

// Moves the baker output read so far to the log; a failed read is logged and returned.
// Needs a running bake (proc set).
int rv_editor_texture_pump(rv_editor_app &app)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    std::string out;
    std::string err;
    std::string read_error;
    const int read_err = bake.proc->read(out, err, read_chunk_bytes, read_error);
    if (read_err != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "baker output not read: " + read_error,
            rv_editor_log_channel::none,
            bake.proc->pid(),
            bake.build_number);
    }
    app.log.add_stream(rv_editor_log_source::build,
        bake.out_partial,
        out,
        rv_editor_log_channel::out,
        bake.proc->pid(),
        bake.build_number);
    app.log.add_stream(rv_editor_log_source::build,
        bake.err_partial,
        err,
        rv_editor_log_channel::err,
        bake.proc->pid(),
        bake.build_number);
    bake.err_all += err;
    return read_err;
}

// Judges the ended baker by its exit and its output file; sets bake.ok and logs the outcome once.
// RV_OK when baked, RV_ERR_IO otherwise. Needs an ended bake (proc set).
int rv_editor_texture_evaluate(rv_editor_app &app)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    const rv_editor_process::rv_editor_exit exit = bake.proc->exit_status();
    std::error_code ec;
    const bool exists = std::filesystem::exists(bake.out, ec);
    bake.ok = exit.signal == 0 && exit.code == 0 && exists;
    if (bake.ok) {
        bake.message = "texture " + bake.name + " baked from " + bake.png.string();
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, bake.message);
        if (exit.guard_result != RV_OK) {
            app.log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                "texture " + bake.name + " bake: process guard: " + exit.guard_error,
                rv_editor_log_channel::none,
                bake.proc->pid(),
                bake.build_number);
        }
        return RV_OK;
    }
    const std::string last = rv_editor_texture_last_line(bake.err_all);
    // rv_editor_exit_text already carries the guard reason for a non-zero exit.
    const bool clean_exit = exit.code == 0 && exit.signal == 0;
    const std::string guard = exit.guard_result != RV_OK ? "; process guard: " + exit.guard_error : "";
    const std::string why = clean_exit ? "the output was not written" + guard : rv_editor_exit_text(exit);
    bake.message =
        "texture " + bake.name + " not baked from " + bake.png.string() + ": " + why + (last.empty() ? "" : " (" + last + ")");
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, bake.message);
    return RV_ERR_IO;
}

} // namespace

bool rv_editor_app_texture_bake_busy(const rv_editor_app &app, std::string *name)
{
    if (app.texture_bake.proc == nullptr) {
        return false;
    }
    if (name != nullptr) {
        *name = app.texture_bake.name;
    }
    return true;
}

int rv_editor_app_texture_bake_start(rv_editor_app &app, const std::string &name, const std::filesystem::path &png)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    if (bake.proc != nullptr) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "texture " + name + " not baked: another bake is already running");
        return RV_ERR_BUSY;
    }
    // A started bake replaces the last outcome.
    bake.message.clear();
    bake.name = name;
    bake.png = png;
    bake.build_number = app.session.build_number();
    const std::filesystem::path staging = app.project.cache_dir / cache_staging_dir / std::to_string(bake.build_number);
    bake.out = staging / name;
    std::error_code ec;
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        bake.ok = false;
        bake.message = "texture " + name + " not baked: " + staging.string() + ": " + ec.message();
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, bake.message);
        return RV_ERR_IO;
    }
    const std::vector<std::string> argv = { app.tools.burner.path.string(),
        std::string(burner_cmd_bake),
        app.project.root.string(),
        png.string(),
        std::string(burner_arg_output),
        bake.out.string(),
        std::string(burner_arg_baker),
        app.tools.baker.path.string() };
    bake.proc = std::make_unique<rv_editor_process>();
    bake.out_partial.clear();
    bake.err_partial.clear();
    bake.err_all.clear();
    std::string error;
    const int start_err = bake.proc->start(argv, app.project.root, error);
    if (start_err != RV_OK) {
        bake.proc.reset();
        bake.ok = false;
        bake.message = "texture " + name + " not baked: " + error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, bake.message);
        return start_err;
    }
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "baking texture " + name + " from " + png.string(),
        rv_editor_log_channel::none,
        bake.proc->pid(),
        bake.build_number);
    return RV_OK;
}

int rv_editor_app_texture_bake_reap(rv_editor_app &app)
{
    if (app.texture_bake.proc == nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "texture bake not reaped: no bake is running");
        return RV_ERR_INVAL;
    }
    return app.texture_bake.proc->poll() && app.texture_bake.proc->output_done() ? RV_OK : RV_ERR_BUSY;
}

bool rv_editor_app_texture_bake_ok(const rv_editor_app &app)
{
    return app.texture_bake.ok;
}

int rv_editor_app_texture_bake_transfer(rv_editor_app &app)
{
    if (app.texture_bake.proc == nullptr) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "texture bake output not moved: no bake is running");
        return RV_ERR_INVAL;
    }
    return rv_editor_texture_pump(app);
}

int rv_editor_app_texture_bake_end(rv_editor_app &app)
{
    if (app.texture_bake.proc == nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "texture bake not ended: no bake is running");
        return RV_ERR_INVAL;
    }
    rv_editor_texture_bake &bake = app.texture_bake;
    const int read_err = rv_editor_texture_pump(app);
    app.log.flush_stream(rv_editor_log_source::build,
        bake.out_partial,
        rv_editor_log_channel::out,
        bake.proc->pid(),
        bake.build_number);
    app.log.flush_stream(rv_editor_log_source::build,
        bake.err_partial,
        rv_editor_log_channel::err,
        bake.proc->pid(),
        bake.build_number);
    if (bake.proc->output_cut()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "texture " + bake.name +
                " baker output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none,
            bake.proc->pid(),
            bake.build_number);
    }
    const int judged = rv_editor_texture_evaluate(app);
    bake.proc.reset();
    return read_err != RV_OK ? read_err : judged;
}

int rv_editor_app_texture_bake_send(rv_editor_app &app)
{
    const rv_editor_texture_bake &bake = app.texture_bake;
    if (!app.session.live() || app.session.reloading()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "texture " + bake.name +
                " baked but not sent: " + (app.session.live() ? "a reload is already in flight" : "the session has ended"));
        return RV_ERR_BUSY;
    }
    std::vector<unsigned char> bytes;
    if (rv_editor_texture_read(bake.out, bytes) != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "texture " + bake.name + " baked but not sent: " + bake.out.string() + " could not be read");
        return RV_ERR_IO;
    }
    std::string error;
    const int sent = app.session.reload_asset(app.log, bake.name, bytes, error);
    if (sent != RV_OK) {
        // Busy is a refusal to retry later (not ready, a reload in flight): a warning.
        const rv_editor_log_level level = sent == RV_ERR_BUSY ? rv_editor_log_level::warning : rv_editor_log_level::error;
        app.log.add(rv_editor_log_source::editor, level, "texture " + bake.name + " baked but not sent: " + error);
    }
    return sent;
}

} // namespace rv_editor
