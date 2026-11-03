#!/usr/bin/env bash
# Runs install.sh, the app and uninstall against a stand-in runtime served from a folder,
# so it needs no network, no Wine and no Rosetta, and touches nothing outside a temporary folder.
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
readonly PROJECT
VERSION="$(cat "${PROJECT}/VERSION")"
readonly VERSION
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "$WORK"' EXIT

export HOME="${WORK}/home"
export TM2020_ROOT="${HOME}/Games/TM2020"
export TM2020_SUPPORT="${HOME}/Library/Application Support/Trackmania2020"
export TM2020_CACHE="${HOME}/Library/Caches/Trackmania2020"
export TM2020_APP="${HOME}/Applications/Trackmania 2020.app"
export TM2020_REPO="file://${WORK}/repo"
export PATH="${WORK}/bin:${PATH}"
readonly PREFIX="${TM2020_ROOT}/prefix"
readonly RUNTIME="${TM2020_SUPPORT}/runtime/${VERSION}"
readonly LAUNCHER="${TM2020_SUPPORT}/launcher"
readonly RELEASE="${WORK}/repo/releases/download/v${VERSION}"
readonly ARCHIVE="${RELEASE}/trackmania2020-macos-${VERSION}-runtime.tar.xz"

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

expect_exit() {
  local wanted="$1" code=0
  shift
  "$@" >"${WORK}/output" 2>&1 <"${INPUT:-/dev/null}" || code=$?
  test "$code" = "$wanted" || fail "$* exited with ${code}, wanted ${wanted}: $(cat "${WORK}/output")"
}

expect_output() {
  grep -qF -- "$1" "${WORK}/output" || fail "wanted '$1' in: $(cat "${WORK}/output")"
}

wait_for_wine() {
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    grep -qF -- "$1" "${WORK}/wine.log" 2>/dev/null && return
    sleep 0.2
  done
  fail "Wine never ran $1"
}

# Rosetta is there as far as install.sh can tell. The alert is written down, not shown.
mkdir -p "${WORK}/bin" "$HOME"
printf '#!/bin/sh\nexit 0\n' >"${WORK}/bin/arch"
printf '#!/bin/sh\nprintf "%%s\\n" "$*" >>"%s/alerts.log"\n' "$WORK" >"${WORK}/bin/osascript"
chmod +x "${WORK}/bin/arch" "${WORK}/bin/osascript"

# A runtime whose Wine only writes down what it was asked to run.
readonly STANDIN="${WORK}/standin/trackmania2020-macos-${VERSION}-runtime"
mkdir -p "${STANDIN}/bin" "${STANDIN}/lib/dxvk/x32" "${STANDIN}/lib/dxvk/x64" "${STANDIN}/libexec"
cat >"${STANDIN}/bin/wine" <<SH
#!/bin/sh
printf '%s\n' "\$*" >>"${WORK}/wine.log"
SH
cp "${STANDIN}/bin/wine" "${STANDIN}/bin/wineserver"
chmod +x "${STANDIN}/bin/wine" "${STANDIN}/bin/wineserver"
for bits in x32 x64; do
  echo "dxgi ${bits}" >"${STANDIN}/lib/dxvk/${bits}/dxgi.dll"
  echo "d3d11 ${bits}" >"${STANDIN}/lib/dxvk/${bits}/d3d11.dll"
done
touch "${STANDIN}/libexec/connect-idle.exe"
mkdir -p "$RELEASE"
tar -cJf "$ARCHIVE" -C "${WORK}/standin" "${STANDIN##*/}"

# Connect is already in the prefix, so the installer doesn't download it.
mkdir -p "${PREFIX}/drive_c/windows/system32" "${PREFIX}/drive_c/windows/syswow64"
mkdir -p "${PREFIX}/drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher"
touch "${PREFIX}/user.reg" "${PREFIX}/system.reg" "${PREFIX}/drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher/UbisoftConnect.exe"
# Wine links the prefix to the user's own Documents, which has to outlive an uninstall.
mkdir -p "${HOME}/Documents/Trackmania" "${PREFIX}/drive_c/users/$(id -un)"
touch "${HOME}/Documents/Trackmania/replay"
ln -s "${HOME}/Documents" "${PREFIX}/drive_c/users/$(id -un)/Documents"

expect_exit 1 "${PROJECT}/install.sh"
expect_output 'has no runtime to download'
test ! -e "$RUNTIME" || fail 'a runtime was installed without a release'

echo '0000000000000000000000000000000000000000000000000000000000000000  runtime.tar.xz' >"${ARCHIVE}.sha256"
expect_exit 1 "${PROJECT}/install.sh"
expect_output 'does not match its checksum'
test ! -e "$RUNTIME" || fail 'a runtime that failed its checksum was installed'

(cd "$RELEASE" && shasum -a 256 "${ARCHIVE##*/}" >"${ARCHIVE}.sha256")
expect_exit 0 "${PROJECT}/install.sh"
expect_output "Trackmania2020-MacOS ${VERSION} is installed"
grep -qxF -- '--version' "${WORK}/wine.log" || fail 'the runtime was not tried before it was installed'
cmp -s "${STANDIN}/bin/wine" "${RUNTIME}/bin/wine" || fail 'the runtime is not installed'
test ! -e "${TM2020_CACHE}/${ARCHIVE##*/}" || fail 'the downloaded archive was left behind'
for file in bin/tm2020 config/dxvk.conf tools/session.py tools/app-icon.js VERSION LICENSE; do
  cmp -s "${PROJECT}/${file}" "${LAUNCHER}/${file}" || fail "the launcher has no copy of ${file}"
done
cmp -s "${RUNTIME}/lib/dxvk/x64/d3d11.dll" "${PREFIX}/drive_c/windows/system32/d3d11.dll" ||
  fail 'the prefix was not set up'
test "$(plutil -extract CFBundleIdentifier raw -o - "${TM2020_APP}/Contents/Info.plist")" = local.trackmania2020-macos.connect ||
  fail 'the app has the wrong identifier'
plutil -lint "${TM2020_APP}/Contents/Info.plist" >/dev/null || fail "the app's Info.plist is broken"
bash -n "${TM2020_APP}/Contents/MacOS/tm2020" || fail "the app's script is broken"
grep -qxF "tm2020=${LAUNCHER// /\\ }/bin/tm2020" "${TM2020_APP}/Contents/MacOS/tm2020" ||
  fail "the app doesn't run the launcher: $(cat "${TM2020_APP}/Contents/MacOS/tm2020")"

expect_exit 0 "${PROJECT}/install.sh"
expect_output "The runtime ${VERSION} is already installed"

# The app works without the checkout, and with nothing else set.
: >"${WORK}/wine.log"
expect_exit 0 env -u TM2020_ROOT -u TM2020_SUPPORT -u TM2020_CACHE -u TM2020_APP -u TM2020_REPO \
  "${TM2020_APP}/Contents/MacOS/tm2020"
wait_for_wine 'UbisoftConnect.exe'
test ! -e "${WORK}/alerts.log" || fail "the app showed an alert: $(cat "${WORK}/alerts.log")"

mv "$RUNTIME" "${RUNTIME}.away"
expect_exit 0 "${TM2020_APP}/Contents/MacOS/tm2020"
grep -qF 'No runtime in' "${WORK}/alerts.log" || fail 'the app did not say why it could not start'
mv "${RUNTIME}.away" "$RUNTIME"

mkdir -p "${WORK}/other.app/Contents"
plutil -create xml1 "${WORK}/other.app/Contents/Info.plist"
plutil -insert CFBundleIdentifier -string com.example.other "${WORK}/other.app/Contents/Info.plist"
expect_exit 1 env TM2020_APP="${WORK}/other.app" "${LAUNCHER}/bin/tm2020" app
expect_output 'is another app'
test "$(plutil -extract CFBundleIdentifier raw -o - "${WORK}/other.app/Contents/Info.plist")" = com.example.other ||
  fail 'another app was overwritten'

printf 'no\n' >"${WORK}/answer"
INPUT="${WORK}/answer" expect_exit 1 "${LAUNCHER}/bin/tm2020" uninstall
expect_output 'Nothing was removed'
test -d "$PREFIX" || fail 'uninstall removed the prefix without a yes'

printf 'yes\n' >"${WORK}/answer"
INPUT="${WORK}/answer" expect_exit 0 "${LAUNCHER}/bin/tm2020" uninstall
for gone in "$TM2020_ROOT" "$TM2020_SUPPORT" "$TM2020_CACHE" "$TM2020_APP"; do
  test ! -e "$gone" || fail "uninstall left ${gone}"
done
test -f "${HOME}/Documents/Trackmania/replay" || fail "uninstall removed the user's Documents"
test -d "${WORK}/other.app" || fail 'uninstall removed another app'

echo 'install.sh tests passed'
