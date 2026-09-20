// The player build's disc-medium slot. 3DMPPC_DEVTOOLS is OFF, so a positional
// disc argument can only be an archive: none of the code that reads a disc out
// of a loose directory is in this binary.
//
// The one thing this build still does with a directory is NAME the refusal.
// std::filesystem::is_directory below is not the development capability - it
// decides nothing about how a disc is read - it only routes the path into
// rv_pcloader::mount_dir(), whose player definition
// (rv_pcloader_livedir_null.cpp) exists for exactly this: to say that booting
// from a loose directory needs -D3DMPPC_DEVTOOLS=ON. Without it the same
// mistake costs the caller "the path does not name a readable file", which is
// true and useless to someone holding an unpacked disc.
#include "rv_pboot_discmedium.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "rv_pconsole/cd/rv_pczipmedium.hpp"
#include "rv_pconsole/rv_pcloader.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_disc_mount(const char *disc_path, rv_pcloader &loader, bool &medium_live)
{
    // Always false here: a player build has no live medium to report, whatever
    // the path turns out to be, because the mount below can only ever succeed
    // for an archive.
    medium_live = false;

    std::error_code ec;
    if (disc_path != nullptr && std::filesystem::is_directory(std::filesystem::path(disc_path), ec)) {
        return loader.mount_dir(disc_path);
    }

    return loader.mount(disc_path);
}

std::unique_ptr<rv_pcmedium> rv_pboot_disc_medium(const char *disc_path, bool /*medium_live*/)
{
    return std::make_unique<rv_pczipmedium>(std::string(disc_path));
}

} // namespace rv_3dmppc
