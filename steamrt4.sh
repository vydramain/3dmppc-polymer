#!/bin/sh
# Runs a command in the Steam Runtime 4 SDK image (Podman) with LLVM 23.1.4 first on PATH.
# Why: builds target Steam Runtime 4 (glibc 2.41, libstdc++ 14 of its SDK) with clang from LLVM 23.1.4, whatever the host has.
# Forms:
#   player:          ./steamrt4.sh sh -c 'cmake -S . -B build -G Ninja && cmake --build build'
#   development kit: ./steamrt4.sh sh -c 'cmake -S . -B build-dev -G Ninja -D3DMPPC_DEVTOOLS=ON && cmake --build build-dev'
# Nested ExternalProject builds run in the same container.
set -eu

# Steam Runtime 4 SDK image, pinned by digest.
IMAGE=registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk@sha256:0827d5839550c228b4e454ae2608ce09e0ff023d2bbb6bfceb9812eb5813b742
# LLVM 23.1.4 Linux x64 release, pinned by SHA-256; loaded on the host because the image has no zstd.
LLVM_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.4/LLVM-23.1.4-Linux-X64.tar.zst
LLVM_SHA256=efdd1dbd089b99bfcfcde7b8eb1236163f2e88df5a981cca4f783ec9cb5e0378
# Top dir of the LLVM tarball; the tree is mounted from inside the extraction dir.
LLVM_TOP=LLVM-23.1.4-Linux-X64
# Standard PATH of the image, searched after the LLVM bin dir.
STD_PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

for tool in podman cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "steamrt4.sh: $tool is not found on the host" >&2
        exit 127
    fi
done

repo=$(dirname -- "$(readlink -f -- "$0")")
cache=${XDG_CACHE_HOME:-$HOME/.cache}/3dmppc
llvm_dir="$cache/llvm-23.1.4"
llvm="$llvm_dir/$LLVM_TOP"
# The image sets HOME to the cwd; a cache home keeps dot-files out of the repo.
home="$cache/home"

mkdir -p "$cache" "$home"
nice -n 19 cmake -DRV_FETCH_URL="$LLVM_URL" -DRV_FETCH_SHA256="$LLVM_SHA256" \
    -DRV_FETCH_FILE="$cache/$LLVM_TOP.tar.zst" -DRV_FETCH_DIR="$llvm_dir" \
    -P "$repo/cmake/rv_fetch.cmake" >&2

if [ ! -x "$llvm/bin/clang" ]; then
    echo "steamrt4.sh: no clang in $llvm/bin after the LLVM load" >&2
    exit 1
fi

if ! podman image exists "$IMAGE"; then
    podman pull "$IMAGE" >&2
fi

# Options go before the image, so later prepends keep them ahead of it.
set -- --rm --userns=keep-id -i -v "$repo:$repo" -v "$llvm:$llvm:ro" -v "$home:$home" -w "$PWD" \
    -e "HOME=$home" -e "PATH=$llvm/bin:$STD_PATH" "$IMAGE" "$@"
case "$PWD" in
"$repo" | "$repo"/*) ;;
*) set -- -v "$PWD:$PWD" "$@" ;;
esac
exec podman run "$@"
