#!/bin/sh
# Starts root/bin/<name> inside a runtime: root/steamrt4-sdk when the delivery has it (devtools),
# else Steam Linux Runtime 4 Platform (player). Copied as <root>/<name> into each delivery.
# The ELF runs only inside this delivery's pressure-vessel; nested calls exec it directly,
# without a nested pressure-vessel launch (run, unruntime or wrap). Needs GNU readlink -f and /proc: Linux only.
set -eu

# Real path, so a symlink to this file still finds its delivery.
me="$(readlink -f "$0")"
root="${me%/*}"
name="${me##*/}"
elf="${root}/bin/${name}"

# Inside check: the markers come from the parent launch. This guards against a nested pressure-vessel launch;
# it is not a security boundary, since any process can set these variables.
if [ "${RV_LAUNCH_ROOT:-}" = "${root}" ] && [ "${container:-}" = pressure-vessel ] && [ -d /run/host ]; then
    # Devtools delivery tools. An unmatched glob stays literal and fails the -d test.
    for d in "${root}"/tools/*/bin; do
        if [ -d "${d}" ]; then
            PATH="${d}${PATH:+:${PATH}}"
            export PATH
        fi
    done
    # run gives its child /dev/null as stdin; the original stdin came through fd 9.
    # Restore it here and unset the marker, so children of the ELF do not repeat this.
    if [ -n "${RV_LAUNCH_FD9:-}" ]; then
        exec 0<&9 9<&-
        unset RV_LAUNCH_FD9
    fi
    exec "${elf}" "$@"
fi

# Devtools delivery: its SDK is the runtime (clang 19, CMake, Ninja, GNU ld, readelf for disc.so).
# The player delivery has none. unruntime, as run uses it, prepares the host environment for
# pressure-vessel (it drops the host's Steam Runtime variables). The SDK is bound in place, never
# copied (3.4 GB), so the host's runtime and copy variables are unset here. The "Cannot modify
# /usr when not copying runtime" warning is therefore expected on every launch.
# Devtools: root/SteamLinuxRuntime_4 is a link to the runtime in bin/firmware/ (the one copy, see rv_devtools_delivery.cmake).
sdk="${root}/steamrt4-sdk"
if [ -d "${sdk}" ]; then
    launcher="${root}/SteamLinuxRuntime_4/pressure-vessel/bin/pressure-vessel-unruntime"
    runtime="--runtime=${sdk}"
    unset PRESSURE_VESSEL_COPY_RUNTIME PRESSURE_VESSEL_RUNTIME PRESSURE_VESSEL_RUNTIME_ARCHIVE
    unset PRESSURE_VESSEL_RUNTIME_BASE PRESSURE_VESSEL_ARCHITECTURES
    # Outside the runtime: its var/ would sit in bin/firmware/ and ship with a burned game.
    state_dir="${root}/var"
else
    # Player delivery only: a devtools delivery always has its SDK, so it never takes this branch.
    launcher="${root}/SteamLinuxRuntime_4/run"
    runtime=
    state_dir="${root}/SteamLinuxRuntime_4/var"
fi

if [ ! -x "${elf}" ]; then
    printf 'rv_launch: not executable: %s\n' "${elf}" >&2
    exit 127
fi

if [ ! -x "${launcher}" ]; then
    printf 'rv_launch: not executable: %s\n' "${launcher}" >&2
    exit 127
fi

# fd 9 carries stdin into the container; a caller's own fd 9 must not be overwritten.
if [ -L /proc/$$/fd/9 ]; then
    printf 'rv_launch: fd 9 is already open; it is needed to pass stdin\n' >&2
    exit 1
fi

# State dir of pressure-vessel: inside the delivery, chosen per runtime above.
PRESSURE_VESSEL_VARIABLE_DIR="${state_dir}"
export PRESSURE_VESSEL_VARIABLE_DIR
export RV_LAUNCH_ROOT="${root}"
export RV_LAUNCH_FD9=1
# run cannot pass stdin itself; a --pass-fd can.
exec 9<&0

# Build argv in front of the program args: options, "--", ELF path, args.
set -- -- "${me}" "$@"
for f in /proc/$$/fd/*; do
    n="${f##*/}"
    # Skip std fds, fd 9 (passed below), fds closed after the glob listed them, and the
    # shell's own script fd: only that one is expected to be close-on-exec (bash opens it so).
    if [ "${n}" -le 2 ] || [ "${n}" -eq 9 ] || [ ! -L "${f}" ] || [ "$(readlink "${f}")" = "${me}" ]; then
        continue
    fi
    set -- --pass-fd="${n}" "$@"
done
# Inner quotes keep a runtime path with spaces one argument; an empty runtime (player) adds none.
set -- ${runtime:+"${runtime}"} --filesystem=host --pass-fd=9 "$@"
exec "${launcher}" "$@"
