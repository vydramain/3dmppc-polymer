include_guard(GLOBAL)

# --- the Linux sysroot ----------------------------------------------------------
# The shipped firmware is built against Valve's Steam Runtime 3 "sniper" SDK sysroot: its glibc 2.31 is the
# Steam Deck baseline, so the firmware runs there. The tarball is 1.2 GB, so it is fetched at build time, not
# at configure, like the emsdk compiler. The hash is the pin: a tarball with other content is refused.
set(RV_LINUX_SYSROOT_BASE_URL https://repo.steampowered.com/steamrt-images-sniper/snapshots)
# The snapshot the sysroot comes from; the URL and the hash below are pinned to it together.
set(RV_LINUX_SYSROOT_SNAPSHOT 3.0.20260928.262393)
# The SDK tarball of that snapshot (amd64 and i386 SDK, sniper; the comma is percent-encoded in the name).
set(RV_LINUX_SYSROOT_FILE com.valvesoftware.SteamRuntime.Sdk-amd64%2Ci386-sniper-sysroot.tar.gz)
set(RV_LINUX_SYSROOT_PINNED_URL "${RV_LINUX_SYSROOT_BASE_URL}/${RV_LINUX_SYSROOT_SNAPSHOT}/${RV_LINUX_SYSROOT_FILE}")
set(RV_LINUX_SYSROOT_SHA256 "adb13b9e124e73e0b2cf25e0022703978ec8e917b9a192dcec61bebd72479521")
# A local copy of the same tarball (a file:// URL) lets the build run offline; the hash stays the same.
set(RV_LINUX_SYSROOT_URL "${RV_LINUX_SYSROOT_PINNED_URL}" CACHE STRING
  "Tarball of the Steam Runtime sniper SDK sysroot; its SHA256 is pinned in rv_linux_sysroot.cmake")

# The tarball root (bin/ etc/ lib/ usr/) is the sysroot. The prepare script keeps its stamp and the
# wayland-scanner wrapper beside it, in the parent directory. The build directory is resolved once, so
# every path below is the one the prepare script sees through REALPATH.
get_filename_component(RV_LINUX_SYSROOT_BUILD_DIR "${CMAKE_BINARY_DIR}" REALPATH)
set(RV_LINUX_SYSROOT_ROOT ${RV_LINUX_SYSROOT_BUILD_DIR}/linux-sysroot)
set(RV_LINUX_SYSROOT_DIR ${RV_LINUX_SYSROOT_ROOT}/sysroot)

include(ExternalProject)
ExternalProject_Add(3dmppc_linux_sysroot
  URL ${RV_LINUX_SYSROOT_URL}
  URL_HASH SHA256=${RV_LINUX_SYSROOT_SHA256}
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  PREFIX ${RV_LINUX_SYSROOT_ROOT}/ep
  SOURCE_DIR ${RV_LINUX_SYSROOT_DIR}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

# The download step extracts. ExternalProject records the URL, the hash and the extract option in its urlinfo
# file, which the download step depends on: a change to the URL or the hash runs the step again.
# A fresh extract leaves the stamp and wrapper of an earlier tree beside it, and the prepare script would skip
# on that stamp, so both are removed first. Each run of this step therefore prepares the tree it has just extracted.
ExternalProject_Add_Step(3dmppc_linux_sysroot prepare
  COMMAND ${CMAKE_COMMAND} -E rm -f
          ${RV_LINUX_SYSROOT_ROOT}/sysroot-prepared.stamp
          ${RV_LINUX_SYSROOT_ROOT}/wayland-scanner
  COMMAND ${CMAKE_COMMAND}
          -DRV_SYSROOT=${RV_LINUX_SYSROOT_DIR}
          -P ${CMAKE_CURRENT_LIST_DIR}/rv_linux_sysroot_prepare.cmake
  DEPENDEES download
  ALWAYS 0
)
