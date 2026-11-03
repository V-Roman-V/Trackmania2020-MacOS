#!/usr/bin/env bash
# Installs Trackmania2020-MacOS for this user: the runtime, Ubisoft Connect, and the
# Trackmania 2020 app that starts it. Without a runtime built by make build, it downloads
# the prebuilt one from this version's GitHub release and checks its SHA-256.
#
# Run it from a copy of the repository, or straight from GitHub, which first fetches the
# files of the latest release:
#
#   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/V-Roman-V/Trackmania2020-MacOS/main/install.sh)"
set -euo pipefail

readonly REPO="${TM2020_REPO:-https://github.com/V-Roman-V/Trackmania2020-MacOS}"

: "${TM2020_SUPPORT:=${HOME}/Library/Application Support/Trackmania2020}"
: "${TM2020_CACHE:=${HOME}/Library/Caches/Trackmania2020}"
export TM2020_SUPPORT TM2020_CACHE

fail() {
  echo "$*" >&2
  exit 1
}

step() {
  printf '\n== %s\n' "$*"
}

download() {
  curl --fail --location --retry 3 --connect-timeout 20 "$@"
}

# Run from GitHub, there is no copy of the repository around this script.
bootstrap() {
  local latest tag source status=0
  # GitHub answers with a redirect to the latest release's page, which ends with its tag.
  latest="$(download --silent --show-error --output /dev/null --write-out '%{url_effective}' "${REPO}/releases/latest")" ||
    fail "Could not reach ${REPO}. Check the internet connection and try again."
  tag="${latest##*/}"
  [[ "$tag" =~ ^v[0-9]+(\.[0-9]+)*$ ]] || fail "${REPO} has no release yet."
  source="${TM2020_CACHE}/source-${tag}"
  rm -rf "$source"
  mkdir -p "$source"
  echo "Downloading Trackmania2020-MacOS ${tag#v}"
  download --silent --show-error "${REPO}/archive/refs/tags/${tag}.tar.gz" | tar -xzf - -C "$source" --strip-components 1
  /bin/bash "${source}/install.sh" || status=$?
  rm -rf "$source"
  exit "$status"
}

if test -z "${BASH_SOURCE[0]:-}"; then
  bootstrap
fi

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT
VERSION="$(cat "${PROJECT}/VERSION")"
readonly VERSION
: "${TM2020_RUNTIME:=${TM2020_SUPPORT}/runtime/${VERSION}}"
export TM2020_RUNTIME
readonly LAUNCHER="${TM2020_SUPPORT}/launcher"
readonly ARCHIVE="trackmania2020-macos-${VERSION}-runtime.tar.xz"

check_mac() {
  local release
  test "$(sysctl -n hw.optional.arm64 2>/dev/null)" = 1 || fail 'This needs a Mac with Apple Silicon (M1 or newer).'
  release="$(sw_vers -productVersion)"
  test "${release%%.*}" -ge 14 || fail "This needs macOS 14 Sonoma or newer, and this Mac has macOS ${release}."
}

install_rosetta() {
  arch -x86_64 /usr/bin/true 2>/dev/null && return
  step 'Installing Rosetta 2, which runs programs made for Intel Macs'
  softwareupdate --install-rosetta --agree-to-license
}

install_runtime() {
  local url="${REPO}/releases/download/v${VERSION}" archive="${TM2020_CACHE}/${ARCHIVE}"
  local partial="${TM2020_RUNTIME}.partial" expected
  if test -x "${TM2020_RUNTIME}/bin/wine"; then
    echo "The runtime ${VERSION} is already installed."
    return
  fi
  step "Downloading the runtime ${VERSION}: Wine, DXVK and MoltenVK with this project's patches"
  mkdir -p "$TM2020_CACHE" "$(dirname "$TM2020_RUNTIME")"
  expected="$(download --silent --show-error "${url}/${ARCHIVE}.sha256" | awk '{print $1}')" ||
    fail "The release ${VERSION} has no runtime to download. To build one yourself, see docs/building.md."
  [[ "$expected" =~ ^[0-9a-f]{64}$ ]] || fail "${url}/${ARCHIVE}.sha256 is not a checksum."
  download --progress-bar --output "$archive" "${url}/${ARCHIVE}"
  if test "$(shasum -a 256 "$archive" | cut -d ' ' -f 1)" != "$expected"; then
    rm -f "$archive"
    fail 'The download does not match its checksum. Please run the installer again.'
  fi
  rm -rf "$partial"
  mkdir -p "$partial"
  tar -xJf "$archive" -C "$partial" --strip-components 1
  "${partial}/bin/wine" --version >/dev/null || fail 'The downloaded runtime does not start.'
  mv "$partial" "$TM2020_RUNTIME"
  rm -f "$archive"
}

# The app runs these copies, so the folder this script came from can go afterwards.
install_launcher() {
  local partial="${LAUNCHER}.partial"
  rm -rf "$partial"
  mkdir -p "${partial}/bin" "${partial}/config" "${partial}/tools"
  cp "${PROJECT}/bin/tm2020" "${partial}/bin/"
  cp "${PROJECT}/config/dxvk.conf" "${partial}/config/"
  cp "${PROJECT}/tools/session.py" "${partial}/tools/"
  cp "${PROJECT}/VERSION" "${PROJECT}/LICENSE" "${partial}/"
  rm -rf "$LAUNCHER"
  mv "$partial" "$LAUNCHER"
}

echo "Installing Trackmania2020-MacOS ${VERSION}"
check_mac
install_rosetta
install_runtime
step 'Setting up Ubisoft Connect'
"${PROJECT}/bin/tm2020" prepare
install_launcher
step 'Making the app'
"${LAUNCHER}/bin/tm2020" app
cat <<EOF

Trackmania2020-MacOS ${VERSION} is installed.

Open Trackmania 2020 from your Applications folder or Launchpad. Ubisoft Connect opens:
sign in, install Trackmania from its page, and press Play.
EOF
