#!/usr/bin/env bash
# Builds the runtime: TrackMania-MacOS's Wine runtime with patches/wine added, plus
# 32-bit and 64-bit DXVK with patches/d9vk.
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
readonly PROJECT

: "${TM2020_SUPPORT:=${HOME}/Library/Application Support/Trackmania2020}"
: "${TM2020_RUNTIME:=${TM2020_SUPPORT}/runtime/$(cat "${PROJECT}/VERSION")}"

readonly TMNF_URL="https://github.com/vakesz/TrackMania-MacOS.git"
readonly TMNF_TAG="v0.4.0"
readonly TMNF_COMMIT="64522ed8725e03e1c5ed2b0becb6703a9b160b77"

readonly BUILD="${PROJECT}/.build/runtime"
readonly TMNF="${BUILD}/TrackMania-MacOS"
readonly RELEASE="${TMNF}/.build/release"

fetch_tmnf() {
  test -d "${TMNF}/.git" || git clone --depth 1 --branch "$TMNF_TAG" "$TMNF_URL" "$TMNF"
  if test "$(git -C "$TMNF" rev-parse HEAD)" != "$TMNF_COMMIT"; then
    echo "${TMNF} is not at ${TMNF_COMMIT}. Remove it and run this again." >&2
    exit 1
  fi
}

# TrackMania-MacOS's builder applies every patch in its folder by name, so the prefix
# puts ours after its own.
build_wine() {
  local patch
  rm -f "${TMNF}/patches/wine"/tm2020-*.patch
  for patch in "${PROJECT}/patches/wine"/*.patch; do
    cp "$patch" "${TMNF}/patches/wine/tm2020-$(basename "$patch")"
  done
  "${TMNF}/src/shell/tools/build-runtime.sh"
}

build_dxvk() {
  local bits="$1" cross_file="$2" patch
  shift 2
  local source="${BUILD}/d9vk-${bits}-source" build="${BUILD}/d9vk-${bits}"
  rm -rf "$source" "$build"
  cp -R "${RELEASE}/sources/d9vk" "$source"
  for patch in "$@"; do
    patch --directory="$source" --strip=1 --fuzz=0 --input="$patch"
  done
  meson setup --cross-file "${source}/${cross_file}" --buildtype=release -Doptimization=3 "$build" "$source"
  ninja -C "$build" -j "${TMNF_JOBS:-$(sysctl -n hw.ncpu)}" src/dxgi/dxgi.dll src/d3d11/d3d11.dll
}

install_runtime() {
  local partial="${TM2020_RUNTIME}.partial" bits
  rm -rf "$partial"
  mkdir -p "$(dirname "$TM2020_RUNTIME")"
  ditto "${RELEASE}/runtime" "$partial"
  for bits in x32 x64; do
    mkdir -p "${partial}/lib/dxvk/${bits}"
    cp "${BUILD}/d9vk-${bits}/src/dxgi/dxgi.dll" "${BUILD}/d9vk-${bits}/src/d3d11/d3d11.dll" "${partial}/lib/dxvk/${bits}/"
  done
  mv "$partial" "$TM2020_RUNTIME"
}

if test -e "$TM2020_RUNTIME"; then
  echo "${TM2020_RUNTIME} already exists. Remove it first, or set TM2020_RUNTIME to another folder." >&2
  exit 1
fi

mkdir -p "$BUILD"
fetch_tmnf
build_wine
PATH="$(brew --prefix)/bin:${PATH}"
# Connect passed Ubisoft's sign-in with only the first patch, so it stays that way.
build_dxvk x32 build-win32.txt "${PROJECT}/patches/d9vk/0001-"*.patch
build_dxvk x64 build-win64.txt "${PROJECT}/patches/d9vk/"*.patch
install_runtime
echo "Runtime installed in ${TM2020_RUNTIME}"
