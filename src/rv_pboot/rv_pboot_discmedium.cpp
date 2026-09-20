// The dev build's disc-medium slot: a positional disc argument may name an
// unpacked directory, exactly the medium mount_dir()/rv_pcdirmedium already
// exist to serve (rv_pcloader_livedir.cpp). See rv_pboot_discmedium.hpp for
// why this is two functions rather than one is-it-a-directory check shared
// by both moments.
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
    std::error_code ec;
    medium_live = disc_path != nullptr &&
        std::filesystem::is_directory(std::filesystem::path(disc_path), ec);
    return medium_live ? loader.mount_dir(disc_path) : loader.mount(disc_path);
}

std::unique_ptr<rv_pcmedium> rv_pboot_disc_medium(const char *disc_path, bool medium_live)
{
    if (medium_live) {
        return std::make_unique<rv_pcdirmedium>(std::string(disc_path));
    }
    return std::make_unique<rv_pczipmedium>(std::string(disc_path));
}

} // namespace rv_3dmppc
