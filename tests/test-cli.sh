#!/usr/bin/env bash
# Runs bin/tm2020 against stand-ins for curl and Wine, so it needs no runtime and no network.
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
readonly PROJECT
readonly TM2020="${PROJECT}/bin/tm2020"
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "$WORK"' EXIT

export TM2020_ROOT="${WORK}/root"
export TM2020_CACHE="${WORK}/cache"
export TM2020_RUNTIME="${WORK}/runtime"
readonly PREFIX="${TM2020_ROOT}/prefix"

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

expect_exit() {
  local wanted="$1" code=0
  shift
  "$@" >"${WORK}/output" 2>&1 || code=$?
  test "$code" = "$wanted" || fail "$* exited with ${code}, wanted ${wanted}: $(cat "${WORK}/output")"
}

expect_output() {
  grep -qF -- "$1" "${WORK}/output" || fail "wanted '$1' in: $(cat "${WORK}/output")"
}

expect_exit 2 "$TM2020"
expect_output 'usage:'

for command in install connect stop; do
  expect_exit 1 "$TM2020" "$command"
  expect_output 'make build'
done

mkdir "${WORK}/bin"
cat >"${WORK}/bin/curl" <<'SH'
#!/bin/sh
printf '%s' "$CURL_RESPONSE"
SH
chmod +x "${WORK}/bin/curl"

check_download() {
  local wanted="$1" response="$2" result="$3"
  CURL_RESPONSE="$response" PATH="${WORK}/bin:${PATH}" expect_exit "$wanted" "$TM2020" check-download
  expect_output "$result"
}

check_download 1 '' 'NO ANSWER'
check_download 1 $'HTTP/1.1 403 Forbidden\r\n\r\n<title>Application Control Violation</title>' 'BLOCKED'
check_download 0 $'HTTP/1.1 403 Forbidden\r\nServer: AkamaiGHost\r\n\r\n' 'OPEN'
check_download 1 $'HTTP/1.1 403 Forbidden\r\nServer: filter\r\n\r\n<title>Page</title>' 'UNKNOWN'

mkdir -p "${TM2020_RUNTIME}/bin" "${TM2020_RUNTIME}/lib/dxvk/x32" "${TM2020_RUNTIME}/lib/dxvk/x64" "${TM2020_RUNTIME}/libexec"
touch "${TM2020_RUNTIME}/libexec/connect-idle.exe"
cat >"${TM2020_RUNTIME}/bin/wine" <<SH
#!/bin/sh
echo "\$WINEDLLOVERRIDES \$*" >>"${WORK}/wine.log"
SH
chmod +x "${TM2020_RUNTIME}/bin/wine"
for bits in x32 x64; do
  echo "dxgi ${bits}" >"${TM2020_RUNTIME}/lib/dxvk/${bits}/dxgi.dll"
  echo "d3d11 ${bits}" >"${TM2020_RUNTIME}/lib/dxvk/${bits}/d3d11.dll"
done
mkdir -p "${PREFIX}/drive_c/windows/system32" "${PREFIX}/drive_c/windows/syswow64"
mkdir -p "${PREFIX}/drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher"
touch "${PREFIX}/user.reg" "${PREFIX}/drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher/UbisoftConnect.exe"

expect_exit 0 "$TM2020" connect
cmp -s "${TM2020_RUNTIME}/lib/dxvk/x64/d3d11.dll" "${PREFIX}/drive_c/windows/system32/d3d11.dll" ||
  fail 'the 64-bit DXVK is not in system32'
cmp -s "${TM2020_RUNTIME}/lib/dxvk/x32/dxgi.dll" "${PREFIX}/drive_c/windows/syswow64/dxgi.dll" ||
  fail 'the 32-bit DXVK is not in syswow64'
grep -qF 'AppDefaults\UplayWebCore.exe\DllOverrides' "${WORK}/wine.log" || fail 'no DLL override for UplayWebCore.exe'
grep -qF 'AppDefaults\Trackmania.exe\DllOverrides' "${WORK}/wine.log" || fail 'no DLL override for Trackmania.exe'
grep -F 'UbisoftConnect.exe' "${WORK}/wine.log" | grep -qF 'mscoree,mshtml=d' || fail 'Wine can offer to install Mono'
grep -qF 'connect-idle.exe' "${WORK}/wine.log" || fail 'the helper was not started'
grep -qF '"hardware_acceleration_mode": {"enabled": false}' \
  "${PREFIX}/drive_c/users/$(id -un)/AppData/Local/Ubisoft Game Launcher/cache/http2/Local State" ||
  fail 'software compositing is not set'

echo 'bin/tm2020 tests passed'
