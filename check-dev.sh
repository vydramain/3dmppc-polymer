#!/bin/sh
# Acceptance run for the v0.4 development runtime.
#
# Not part of the console and not part of any build: this is the developer-side
# scenario that proves the guarantees in docs/development-runtime.md still hold.
# It builds what it needs, burns the sample disc both ways, and hands the work
# to tests/dev_runtime/check_dev.py, whose exit code is the verdict.
#
# Usage: ./check-dev.sh
set -eu

cd "$(dirname "$0")"

CONSOLE=build/pconsole/3dmppc
BURNER=pdk/tools/build/mppcburner/mppcburner
BAKER=pdk/tools/build/mppcbaker/mppcbaker

echo "== building the console and the tools =="
cmake --build build >/dev/null
cmake --build pdk/tools/build >/dev/null

echo "== burning the sample disc both ways =="
# The archive is the shipped form; the directory is the development form. Both
# must produce the same machine, which is one of the things the run checks.
"$BURNER" build mppcdiscs/example-lua -o build/example-lua.mppcdisc --baker "$BAKER" >/dev/null 2>&1
"$BURNER" build mppcdiscs/example-lua --unpacked build/example-lua.discdir --baker "$BAKER" >/dev/null 2>&1

echo "== driving the development channel =="
exec python3 tests/dev_runtime/check_dev.py
