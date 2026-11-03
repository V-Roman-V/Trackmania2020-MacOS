#!/usr/bin/env bash
# Packs the runtime that make build installed for a GitHub release, with the source it was
# built from, into .build/dist:
#
#   trackmania2020-macos-<VERSION>-runtime.tar.xz, which install.sh downloads
#   trackmania2020-macos-<VERSION>-source.tar.xz, this repository and the upstream code
#
# and a .sha256 file for each. Wine and several libraries are LGPL, so their source goes
# into the same release as the runtime.
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
readonly PROJECT
VERSION="$(cat "${PROJECT}/VERSION")"
readonly VERSION

: "${TM2020_SUPPORT:=${HOME}/Library/Application Support/Trackmania2020}"
: "${TM2020_RUNTIME:=${TM2020_SUPPORT}/runtime/${VERSION}}"

readonly BUILD="${PROJECT}/.build/runtime"
readonly DIST="${PROJECT}/.build/dist"
readonly NAME="trackmania2020-macos-${VERSION}"
# Archives carry no owner names from this Mac, and no Finder metadata.
readonly TAR=(env COPYFILE_DISABLE=1 tar --no-mac-metadata --uid 0 --gid 0 --uname root --gname wheel
  --options xz:threads=0)

fail() {
  echo "$*" >&2
  exit 1
}

# The runtime has to come from the committed state of everything that goes into it.
check_runtime() {
  local info="${TM2020_RUNTIME}/build-info.txt" built inputs
  test -x "${TM2020_RUNTIME}/bin/wine" || fail "No runtime in ${TM2020_RUNTIME}. Run make build first."
  test "$(head -n 1 "$info")" = "Trackmania2020-MacOS runtime ${VERSION}" ||
    fail "${TM2020_RUNTIME} was not built as version ${VERSION}."
  built="$(sed -n 's/^Commit //p' "$info")"
  [[ "$built" =~ ^[0-9a-f]{40}$ ]] || fail "The runtime was built from: ${built:-an unknown state}. Build it again from a commit."
  test -z "$(git -C "$PROJECT" status --porcelain)" || fail 'Commit or stash your changes first, so the source archive matches a commit.'
  read -r -a inputs < <(sed -n 's/^INPUTS=(\(.*\))$/\1/p' "${PROJECT}/tools/build-runtime.sh")
  git -C "$PROJECT" diff --quiet "$built" HEAD -- "${inputs[@]}" ||
    fail "What goes into the runtime changed after ${built:0:12}, which it was built from. Run make build again."
  test -f "${BUILD}/sources/wine/.tm2020-source" || fail "The sources in ${BUILD}/sources are missing. Run make build again."
}

pack_runtime() {
  echo "Packing ${TM2020_RUNTIME}"
  "${TAR[@]}" -cJf "${DIST}/${NAME}-runtime.tar.xz" -C "$(dirname "$TM2020_RUNTIME")" "$(basename "$TM2020_RUNTIME")"
}

# Wine as patched for the build, the other trees as fetched, and the libraries' original
# archives. Build output in MoltenVK's tree stays out.
pack_source() {
  echo 'Packing the source'
  git -C "$PROJECT" archive --format=tar --prefix="${NAME}-source/" HEAD >"${DIST}/repository.tar"
  "${TAR[@]}" -cJf "${DIST}/${NAME}-source.tar.xz" --exclude .git \
    --exclude 'sources/moltenvk/External/build' --exclude 'sources/moltenvk/Package' \
    -s ",^sources/,${NAME}-source/upstream/," -s ",^downloads/,${NAME}-source/upstream/archives/," \
    "@${DIST}/repository.tar" -C "$BUILD" sources/wine sources/d9vk sources/moltenvk downloads
  rm -f "${DIST}/repository.tar"
}

check_runtime
rm -rf "$DIST"
mkdir -p "$DIST"
pack_runtime
pack_source
cd "$DIST"
for archive in "${NAME}-runtime.tar.xz" "${NAME}-source.tar.xz"; do
  shasum -a 256 "$archive" >"${archive}.sha256"
done
ls -lh "$DIST"
echo "Attach these four files to the release v${VERSION}."
