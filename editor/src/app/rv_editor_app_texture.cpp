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

// The bake ended (successfully or not): logs it, keeps the outcome, sends on success.
void rv_editor_texture_bake_finish(rv_editor_app &app)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    const rv_editor_process::rv_editor_exit exit = bake.proc->exit_status();
    std::error_code ec;
    const bool exists = std::filesystem::exists(bake.out, ec);
    bake.ok = exit.signal == 0 && exit.code == 0 && exists;
    if (bake.ok) {
        bake.message = "texture " + bake.name + " baked from " + bake.png.string();
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, bake.message);
    } else {
        const std::string last = rv_editor_texture_last_line(bake.err_all);
        const std::string why = exit.code == 0 && exit.signal == 0 ? "the output was not written" : rv_editor_exit_text(exit);
        bake.message = "texture " + bake.name + " not baked from " + bake.png.string() + ": " + why +
            (last.empty() ? "" : " (" + last + ")");
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, bake.message);
    }
    const std::string name = bake.name;
    const std::filesystem::path out = bake.out;
    bake.proc.reset();
    if (!bake.ok) {
        return;
    }
    if (!app.session.live() || app.session.reloading()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "texture " + name +
                " baked but not sent: " + (app.session.live() ? "a reload is already in flight" : "the session has ended"));
        return;
    }
    std::vector<unsigned char> bytes;
    if (rv_editor_texture_read(out, bytes) != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "texture " + name + " baked but not sent: " + out.string() + " could not be read");
        return;
    }
    app.session.reload_asset(app.log, name, bytes);
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

void rv_editor_app_texture_bake_start(rv_editor_app &app, const std::string &name, const std::filesystem::path &png)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    if (bake.proc != nullptr) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "texture " + name + " not baked: another bake is already running");
        return;
    }
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
        return;
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
    if (bake.proc->start(argv, app.project.root, error) != RV_OK) {
        bake.proc.reset();
        bake.ok = false;
        bake.message = "texture " + name + " not baked: " + error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, bake.message);
        return;
    }
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "baking texture " + name + " from " + png.string(),
        rv_editor_log_channel::none,
        bake.proc->pid(),
        bake.build_number);
}

void rv_editor_app_texture_bake_update(rv_editor_app &app)
{
    rv_editor_texture_bake &bake = app.texture_bake;
    if (bake.proc == nullptr) {
        return;
    }
    std::string out;
    std::string err;
    bake.proc->read(out, err, read_chunk_bytes);
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
    if (!bake.proc->poll() || !bake.proc->output_done()) {
        return;
    }
    out.clear();
    err.clear();
    bake.proc->read(out, err, read_chunk_bytes);
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
    rv_editor_texture_bake_finish(app);
}

} // namespace rv_editor
