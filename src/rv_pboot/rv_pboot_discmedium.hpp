// The dev-capability slot for the positional disc argument: whether it may
// name an unpacked directory (mppcburner's --unpacked output) instead of a
// frozen `.mppcdisc` archive. The boot asks this at two different moments -
// rv_pboot_budget_select() has to pick mount()/mount_dir() before the budget
// is even checked, and rv_pboot_run() has to pick which rv_pcmedium to insert
// only after the disc's code is up - so this is two functions, one per
// moment, rather than one question asked twice from two files the way
// rv_pboot_disc_is_directory() used to. Same shape as the slot one layer
// down (rv_pcloader_livedir_devtools.cpp, dev build only): mount_dir() is not even
// declared when this binary was built without -D3DMPPC_DEVTOOLS=ON. On top of
// that build-time gate, a directory is only ever a DRIVE's business when
// --dev was also given on the command line - see rv_pboot_disc_mount() below.
#pragma once

#include <cstdint>
#include <memory>

#include "rv_pconsole/cd/rv_pcmedium.hpp"

namespace rv_3dmppc
{

class rv_pcloader;

// Mounts `disc_path` into `loader` and reports whether the mount came from a
// directory in `medium_live`. A dev build asks the filesystem and routes a
// directory through mount_dir(), an archive through mount() - but only when
// `dev` says --dev was given; the owner's rule is that a loose directory is a
// --dev privilege, not merely a devtools-build one, so a dev build handed a
// directory without the flag refuses it the same way a player build always
// does. A player build always calls mount(), always reports medium_live
// false, and never even looks at `dev` - it carries no
// std::filesystem::is_directory call to ask otherwise, and no mount_dir() to
// route into. `dev` is passed in rather than read from a global so this
// function stays testable from either build without touching rv_pboot_args.
// Returns RV_OK, or a negative rv_err after `loader` (or this function
// itself, for the directory-needs-a-build/flag refusals) has already logged
// exactly what went wrong.
int64_t rv_pboot_disc_mount(const char *disc_path, bool dev, rv_pcloader &loader, bool &medium_live);

// The medium to insert for a disc mounted at `disc_path`, given the same
// `medium_live` rv_pboot_disc_mount() reported for that path: a live
// directory medium when it came from one, the frozen archive medium
// otherwise. A player build's `medium_live` is always false, so this always
// returns the archive medium there.
std::unique_ptr<rv_pcmedium> rv_pboot_disc_medium(const char *disc_path, bool medium_live);

} // namespace rv_3dmppc
