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
    std::string dir = (tmpdir != nullptr && *tmpdir != '\0') ? std::string(tmpdir) : "/tmp";
    while (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }
    return dir;
}

} // namespace

namespace rv_pcloader_detail
{

namespace
{

// Remove the staging file and clear its path so it doesn't leak to the caller.
void discard_staging(std::string &out_path)
{
    ::unlink(out_path.c_str());
    out_path.clear();
}

// Write the entire code buffer, handling EINTR. Returns RV_OK on success,
// RV_ERR_IO on write error.
int write_all(int fd, const std::vector<unsigned char> &code, std::string &why)
{
    std::size_t written = 0;
    while (written < code.size()) {
        const ssize_t rc = ::write(fd, code.data() + written, code.size() - written);
        const bool interrupted = rc < 0 && errno == EINTR;
        if (interrupted) {
            continue;
        }
        if (rc < 0) {
            why = std::format("cannot write the staging file: {}", std::strerror(errno));
            return RV_ERR_IO;
        }
        if (rc == 0) {
            why = "the staging file accepted no bytes";
            return RV_ERR_IO;
        }
        written += static_cast<std::size_t>(rc);
    }
    return RV_OK;
}

} // namespace

// Write `code` to a fresh private file and hand back its path.
//
// mkstemp is what makes the name unpredictable: the console must not open a
// path an attacker could have guessed and pre-created as a symlink to something
// else, which is exactly the classic /tmp race. mkstemp creates with O_EXCL and
// mode 0600, and the fchmod below adds only the execute bit the mapping needs -
// 0700 total, this user and nobody else. A group- or world-writable staging
// file would be a way to swap the disc's code out between this write and the
// dlopen a few lines later.
int extract_code(const std::vector<unsigned char> &code, std::string &out_path, std::string &why)
{
    const std::string dir = staging_dir();

    std::string tmpl = dir + "/mppcdisc-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    const int fd = ::mkstemp(name.data());
    if (fd < 0) {
        why = std::format("cannot create a staging file in '{}': {}", dir, std::strerror(errno));
        return RV_ERR_IO;
    }

    // From here on the file exists, so every failure path must remove it.
    out_path.assign(name.data());

    if (::fchmod(fd, S_IRWXU) != 0) {
        why = std::format("cannot make the staging file executable: {}", std::strerror(errno));
        ::close(fd);
        discard_staging(out_path);
        return RV_ERR_IO;
    }

    const int write_rc = write_all(fd, code, why);
    if (write_rc != RV_OK) {
        ::close(fd);
        discard_staging(out_path);
        return write_rc;
    }

    const int close_rc = ::close(fd);
    if (close_rc != 0) {
        // A close() that fails is a write that did not land - the mapping the
        // loader is about to make would be of a truncated object file.
        why = std::format("cannot close the staging file: {}", std::strerror(errno));
        discard_staging(out_path);
        return RV_ERR_IO;
    }
    return RV_OK;
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
        RV_LOG_ERR("pcloader", "staging directory '{}' cannot be used to extract a disc's code: {}", dir, std::strerror(errno));
        return RV_ERR_IO;
    }
    ::close(fd);

    if (::unlink(name.data()) != 0 && errno != ENOENT) {
        RV_LOG_ERR("pcloader",
            "staging directory '{}' accepted a probe file but would not remove it: {}",
            dir,
            std::strerror(errno));
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace rv_3dmppc
