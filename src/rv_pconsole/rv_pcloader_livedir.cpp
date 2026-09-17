// rv_pcloader: the live-directory mount. This is the development medium - the
// drive publishes the author's own files straight off disk, so the medium can
// change under a running console. Never the player's path.
#include "rv_pconsole/rv_pcloader.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// The manifest's service entry. It lives in the archive next to the assets but
// is NOT part of the disc's resource namespace: the console reads it, the game
// never asks for it. (The code entry's name is the other one, and lives in
// rv_pcloader_detail.hpp because bring_up() needs it too.)
constexpr const char *RV_PCLOADER_MANIFEST_ENTRY = "disc.toml";

// Ceilings on what the console will allocate on the say-so of an archive. Every
// size below comes out of a file that may have been downloaded from anywhere,
// so none of them may become a malloc argument unchecked: a two-line header
// claiming a four-gigabyte manifest must cost a log line, not the machine's
// memory. (The code ceiling states the same policy in
// rv_pcloader_detail.hpp, where both mount routes and bring_up() can see it.)
constexpr int64_t RV_PCLOADER_MANIFEST_MAX_SIZE = 1 << 20; // 1 MiB of text is already absurd

} // namespace

namespace rv_pcloader_detail
{

// Same contract as read_whole_entry() above, off a plain file instead of a
// zip entry - the directory route's counterpart, so mount_dir() can share
// every check downstream of "here are the bytes" with mount().
std::string read_whole_file(const std::filesystem::path &path, int64_t max_size,
    std::vector<unsigned char> &out)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return "no such file";
    }

    const uintmax_t raw_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::format("cannot measure the file: {}", ec.message());
    }
    const int64_t size = static_cast<int64_t>(raw_size);
    if (size > max_size) {
        return std::format("file is {} bytes, over the {} byte ceiling", size,
            max_size);
    }

    try {
        out.assign(static_cast<std::size_t>(size), 0);
    } catch (const std::bad_alloc &) {
        return "out of memory";
    }
    if (size == 0) {
        return std::string();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "cannot open the file";
    }
    if (!in.read(reinterpret_cast<char *>(out.data()), size)) {
        return "short read";
    }
    return std::string();
}

} // namespace rv_pcloader_detail

bool rv_devtools_built()
{
    return true;
}

// The manifest of a directory disc: same parser, same ceiling as mount(). Its
// own step because reading a file and parsing what is in it are two ways to
// fail and mount_dir has five more of its own.
int64_t rv_pcloader::read_dir_manifest_(const std::filesystem::path &root, const char *dir_path)
{
    std::vector<unsigned char> manifest_bytes;
    const std::string why = read_whole_file(root / RV_PCLOADER_MANIFEST_ENTRY,
        RV_PCLOADER_MANIFEST_MAX_SIZE, manifest_bytes);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader", "'{}' carries no usable '{}': {}",
            rv_pdklib::rv_log_escape(dir_path), RV_PCLOADER_MANIFEST_ENTRY, why);
        return RV_ERR_NOENT;
    }

    const std::string manifest_text(
        reinterpret_cast<const char *>(manifest_bytes.data()), manifest_bytes.size());

    std::string merror;
    if (rv_pdklib::rv_manifest_parse(manifest_text, RV_PCLOADER_MANIFEST_ENTRY, manifest_, merror)
        != 0) {
        RV_LOG_ERR("pcloader", "'{}' carries a '{}' that does not parse: {}",
            rv_pdklib::rv_log_escape(dir_path), RV_PCLOADER_MANIFEST_ENTRY,
            rv_pdklib::rv_log_escape(merror.c_str(), 512));
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The lua triple, exactly as mount() enforces it: [scripts] sources, a script
// memory budget and an entry name are all three or none, and a named entry has
// to actually be on the medium.
int64_t rv_pcloader::check_dir_lua_triple_(const std::filesystem::path &root)
{
    const rv_pdklib::rv_manifest_budget_pccl &pccl = manifest_.budget.pccl;
    const bool lua_scripts = !manifest_.scripts_sources.empty();
    const bool lua_memory = pccl.script_memory_size > 0;
    const bool lua_entry = !pccl.script_entry.empty();

    if (lua_scripts != lua_memory || lua_memory != lua_entry) {
        RV_LOG_ERR("pcloader",
            "disc '{}' is neither a lua disc nor a C++ disc: [scripts] sources {}, "
            "script_memory_size={}, script_entry='{}'. All three or none",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            lua_scripts ? "stated" : "absent", pccl.script_memory_size,
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }

    std::error_code entry_ec;
    if (lua_entry && !std::filesystem::is_regular_file(root / pccl.script_entry, entry_ec)) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names '{}' as its lua entry, but the drive has no such asset",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int64_t rv_pcloader::mount_dir(const char *dir_path)
{
    unload();

    if (dir_path == nullptr || *dir_path == '\0') {
        RV_LOG_ERR("pcloader", "no disc path was given");
        return RV_ERR_INVAL;
    }

    std::error_code ec;
    const std::filesystem::path root(dir_path);
    if (!std::filesystem::is_directory(root, ec)) {
        RV_LOG_ERR("pcloader",
            "no disc at '{}': the path does not name a readable directory",
            rv_pdklib::rv_log_escape(dir_path));
        return RV_ERR_NOENT;
    }

    const int64_t manifest_rc = read_dir_manifest_(root, dir_path);
    if (manifest_rc < 0) {
        return manifest_rc;
    }

    const int64_t lua_rc = check_dir_lua_triple_(root);
    if (lua_rc < 0) {
        return lua_rc;
    }

    const std::string code_entry = code_entry_of(manifest_);

    // The code entry's bytes, under the same ceiling bring_up() would apply -
    // read ONCE, here, so the bytes pre_dlopen_check_bytes() inspects below
    // are the exact bytes bring_up() later hands to extract_code(). See
    // dir_code_'s comment in rv_pcloader.hpp for why a second read from the
    // path is not an option for a directory disc.
    std::vector<unsigned char> code_bytes;
    const std::string why = read_whole_file(root / code_entry, RV_PCLOADER_CODE_MAX_SIZE, code_bytes);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names its code entry '{}', which is unusable: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(code_entry.c_str()), why);
        return RV_ERR_NOENT;
    }

    if (pre_dlopen_check_bytes(code_bytes, code_entry.c_str(), dir_path) < 0) {
        RV_LOG_ERR("pcloader",
            "disc '{}' failed the pre-load inspection of its code entry - "
            "the exact "
            "refusal is in the log line above; its code will not be mapped",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()));
        return RV_ERR_INVAL;
    }

    dir_path_ = dir_path;
    dir_code_ = std::move(code_bytes);
    from_directory_ = true;
    return RV_OK;
}

} // namespace rv_3dmppc
