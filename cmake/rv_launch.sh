#!/bin/sh
# Starts root/bin/<name> inside Steam Linux Runtime 4. Copied as <root>/<name> into each delivery.
# The ELF runs only inside this delivery's pressure-vessel; nested calls exec it directly,
# because a nested `run` fails. Needs GNU readlink -f and /proc: Linux only.
set -eu

# Real path, so a symlink to this file still finds its delivery.
me="$(readlink -f "$0")"
root="${me%/*}"
name="${me##*/}"
elf="${root}/bin/${name}"
run="${root}/SteamLinuxRuntime_4/run"

# Inside check: the markers come from the parent launch. This guards against a nested run;
# it is not a security boundary, since any process can set these variables.
if [ "${RV_LAUNCH_ROOT:-}" = "${root}" ] && [ "${container:-}" = pressure-vessel ] && [ -d /run/host ]; then
    # Development kit tools. An unmatched glob stays literal and fails the -d test.
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

if [ ! -x "${elf}" ]; then
    printf 'rv_launch: not executable: %s\n' "${elf}" >&2
    exit 127
fi

if [ ! -x "${run}" ]; then
    printf 'rv_launch: not executable: %s\n' "${run}" >&2
    exit 127
fi

# fd 9 carries stdin into the container; a caller's own fd 9 must not be overwritten.
if [ -L /proc/$$/fd/9 ]; then
    printf 'rv_launch: fd 9 is already open; it is needed to pass stdin\n' >&2
    exit 1
fi

# State dir inside the delivery, as Steam keeps it.
PRESSURE_VESSEL_VARIABLE_DIR="${root}/SteamLinuxRuntime_4/var"
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
set -- --filesystem=host --pass-fd=9 "$@"
exec "${run}" "$@"
