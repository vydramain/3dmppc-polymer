// rv_pcloader: staging the disc's code before dlopen - a private temp file.
#include "rv_pconsole/rv_pcloader.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// TMPDIR or /tmp, trailing slashes trimmed. The one place both extract_code()
// and rv_pcloader_probe_staging() decide where the extracted disc.so lives -
// factored out so the two can never drift onto different directories.
std::string staging_dir()
{
    const char *tmpdir = std::getenv("TMPDIR");
    std::string dir =
        (tmpdir != nullptr && *tmpdir != '\0') ? std::string(tmpdir) : "/tmp";
    while (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }
    return dir;
}

} // namespace

namespace rv_pcloader_detail
{

// Write `code` to a fresh private file and hand back its path.
//
// mkstemp is what makes the name unpredictable: the console must not open a
// path an attacker could have guessed and pre-created as a symlink to something
// else, which is exactly the classic /tmp race. mkstemp creates with O_EXCL and
// mode 0600, and the fchmod below adds only the execute bit the mapping needs -
// 0700 total, this user and nobody else. A group- or world-writable staging
// file would be a way to swap the disc's code out between this write and the
// dlopen a few lines later.
std::string extract_code(const std::vector<unsigned char> &code,
    std::string &out_path)
{
    const std::string dir = staging_dir();

    std::string tmpl = dir + "/mppcdisc-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    const int fd = ::mkstemp(name.data());
    if (fd < 0) {
        return std::format("cannot create a staging file in '{}': {}", dir,
            std::strerror(errno));
    }

    // From here on the file exists, so every failure path must remove it.
    out_path.assign(name.data());

    std::string error;
    if (::fchmod(fd, S_IRWXU) != 0) {
        error = std::format("cannot make the staging file executable: {}",
            std::strerror(errno));
    }

    std::size_t written = 0;
    while (error.empty() && written < code.size()) {
        const ssize_t rc =
            ::write(fd, code.data() + written, code.size() - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::format("cannot write the staging file: {}",
                std::strerror(errno));
            break;
        }
        if (rc == 0) {
            error = "the staging file accepted no bytes";
            break;
        }
        written += static_cast<std::size_t>(rc);
    }

    if (::close(fd) != 0 && error.empty()) {
        // A close() that fails is a write that did not land - the mapping the
        // loader is about to make would be of a truncated object file.
        error =
            std::format("cannot close the staging file: {}", std::strerror(errno));
    }

    if (!error.empty()) {
        ::unlink(out_path.c_str());
        out_path.clear();
    }
    return error;
}

} // namespace rv_pcloader_detail

// Is the staging area an extracted disc.so will need actually
// usable? Creates and removes a probe file in the same directory
// extract_code() would use (staging_dir(), above - the one helper both this
// function and extract_code() share, so they can never disagree on the
// directory). Returns RV_OK, or a negative rv_err after logging the
// directory and why it cannot be used.
int64_t rv_pcloader_probe_staging()
{
    const std::string dir = staging_dir();

    std::string tmpl = dir + "/mppcdisc-probe-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    const int fd = ::mkstemp(name.data());
    if (fd < 0) {
        RV_LOG_ERR("pcloader",
            "staging directory '{}' cannot be used to extract a disc's code: {}",
            dir, std::strerror(errno));
        return RV_ERR_IO;
    }
    ::close(fd);

    if (::unlink(name.data()) != 0 && errno != ENOENT) {
        RV_LOG_ERR("pcloader",
            "staging directory '{}' accepted a probe file but would not remove it: {}",
            dir, std::strerror(errno));
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace rv_3dmppc
