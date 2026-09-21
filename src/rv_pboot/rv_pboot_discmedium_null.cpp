// The player build's disc-medium slot. 3DMPPC_DEVTOOLS is OFF, so a positional
// disc argument can only be an archive: none of the code that reads a disc out
// of a loose directory is in this binary - rv_pcloader::mount_dir() is not
// even DECLARED here (rv_pconsole/rv_pcloader.hpp), so this file cannot route
// into it the way the dev build's rv_pboot_discmedium.cpp does.
//
// The one thing this build still does with a directory is NAME the refusal.
// std::filesystem::is_directory below is not the development capability - it
// decides nothing about how a disc is read - it only chooses which sentence
// to log: the same one rv_pcloader_livedir_null.cpp used to give, back when a
// player build still linked a stub mount_dir() to say it through. Without
// this check the same mistake costs the caller "the path does not name a
// readable file", which is true and useless to someone holding an unpacked
// disc.
#include "rv_pboot_discmedium.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pczipmedium.hpp"
#include "rv_pconsole/rv_pcloader.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_disc_mount(const char *disc_path, bool /*dev*/, rv_pcloader &loader, bool &medium_live)
{
    // Always false here: a player build has no live medium to report, whatever
    // the path turns out to be, because the mount below can only ever succeed
    // for an archive.
    medium_live = false;

    std::error_code ec;
    if (disc_path != nullptr && std::filesystem::is_directory(std::filesystem::path(disc_path), ec)) {
        RV_LOG_ERR("pcloader",
            "this console was built without the development runtime: booting from "
            "a loose directory needs -D3DMPPC_DEVTOOLS=ON");
        return RV_ERR_NOENT;
    }

    return loader.mount(disc_path);
}

std::unique_ptr<rv_pcmedium> rv_pboot_disc_medium(const char *disc_path, bool /*medium_live*/)
{
    return std::make_unique<rv_pczipmedium>(std::string(disc_path));
}

} // namespace rv_3dmppc
