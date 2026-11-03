#!/usr/bin/env bash
# Builds the runtime from the sources pinned in config/sources.json: Wine with
# patches/wine, the libraries it needs, MoltenVK, and 32-bit and 64-bit DXVK with
# patches/d9vk. The steps follow the builder of TrackMania-MacOS (MIT license,
# Copyright (c) 2026 TMNF contributors), without the parts only TMNF uses.
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
readonly PROJECT

: "${TM2020_SUPPORT:=${HOME}/Library/Application Support/Trackmania2020}"
: "${TM2020_RUNTIME:=${TM2020_SUPPORT}/runtime/$(cat "${PROJECT}/VERSION")}"
: "${TM2020_JOBS:=$(sysctl -n hw.ncpu)}"

readonly BUILD="${PROJECT}/.build/runtime"
readonly SOURCES="${BUILD}/sources"
readonly DOWNLOADS="${BUILD}/downloads"
readonly WORK="${BUILD}/work"
readonly DEPS="${WORK}/deps"
readonly TREE="${WORK}/runtime"
readonly LOG="${BUILD}/build.log"
readonly PINS="${PROJECT}/config/sources.json"
# Keeps the checkout path out of compiled code. check_paths makes sure of that.
readonly PREFIX_MAP="-ffile-prefix-map=${PROJECT}=/tm2020"
FORMULAS=(mingw-w64 meson ninja glslang bison flex pkgconf)
LIBRARIES=(freetype gmp nettle libtasn1 libunistring gnutls)
LIBRARY_ENV=("CC=/usr/bin/clang -arch x86_64" "CXX=/usr/bin/clang++ -arch x86_64"
  "CFLAGS=-O2 ${PREFIX_MAP}" "CXXFLAGS=-O2 ${PREFIX_MAP}" "CPPFLAGS=-I${DEPS}/include"
  "LDFLAGS=-L${DEPS}/lib -Wl,-headerpad_max_install_names"
  "PKG_CONFIG_LIBDIR=${DEPS}/lib/pkgconfig" "PKG_CONFIG_PATH=")
export MACOSX_DEPLOYMENT_TARGET=14.0

fail() {
  echo "$*" >&2
  exit 1
}

# Runs a command with its output in the build log, and shows the end of the log if it fails.
run() {
  local title="$1" started="$SECONDS"
  shift
  printf '  %s ... ' "$title"
  printf '\n$ %s\n' "$*" >>"$LOG"
  if ! "$@" >>"$LOG" 2>&1; then
    echo failed
    tail -n 40 "$LOG" >&2
    fail "The full log is ${LOG}"
  fi
  echo "$((SECONDS - started)) s"
}

run_in() {
  local directory="$1"
  shift
  (cd "$directory" && run "$@")
}

check_host() {
  local formula missing=()
  test "$(uname -s)/$(uname -m)" = Darwin/arm64 || fail 'This needs an Apple Silicon Mac.'
  case "$PROJECT" in
    *[[:space:]]*) fail "Wine's makefiles need a checkout path without spaces." ;;
  esac
  [[ "$TM2020_JOBS" =~ ^[1-9][0-9]*$ ]] || fail 'TM2020_JOBS has to be a positive number.'
  command -v brew >/dev/null || fail 'Install Homebrew from https://brew.sh first.'
  xcodebuild -version >/dev/null 2>&1 ||
    fail 'Select the full Xcode app first: sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer'
  /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null ||
    fail 'Install Rosetta first: softwareupdate --install-rosetta --agree-to-license'
  for formula in "${FORMULAS[@]}"; do
    brew list --versions "$formula" >/dev/null 2>&1 || missing+=("$formula")
  done
  if test "${#missing[@]}" -gt 0; then brew install "${missing[@]}"; fi
  # macOS's own bison is too old for Wine, and Homebrew keeps its bison and flex off PATH.
  PATH="$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$(brew --prefix)/bin:/usr/bin:/bin:/usr/sbin:/sbin"
  export PATH
}

# Prints one field of a source's pin, or nothing if the pin has no such field.
pin() {
  plutil -extract "$1.$2" raw -o - "$PINS" 2>/dev/null || true
}

sha256() {
  shasum -a 256 "$1" | cut -d ' ' -f 1
}

# Prepares sources/<name>: the pinned code with the given patches applied. A stamp records
# the pin and the patches, so changing either prepares the tree again.
fetch() {
  local name="$1" dest="${SOURCES}/$1" part="${SOURCES}/$1.partial"
  shift
  local stamp git revision url sum archive patch
  stamp="$(plutil -extract "$name" json -o - "$PINS"
    for patch in "$@"; do echo "$(sha256 "$patch") ${patch#"${PROJECT}"/}"; done)"
  if test -f "${dest}/.tm2020-source" && test "$(cat "${dest}/.tm2020-source")" = "$stamp"; then
    return
  fi
  rm -rf "$part"
  git="$(pin "$name" git)"
  revision="$(pin "$name" revision)"
  if test -n "$git" && test -d "${dest}/.git" && test "$(git -C "$dest" rev-parse HEAD)" = "$revision"; then
    # Same revision with other patches: only the files they touch change, so make rebuilds little.
    git -C "$dest" reset -q --hard
    git -C "$dest" clean -q -f -d -x
    mv "$dest" "$part"
  elif test -n "$git"; then
    rm -rf "$dest"
    mkdir -p "$part"
    git -C "$part" init -q
    run "${name}: fetching ${revision:0:12}" git -C "$part" fetch --depth=1 "$git" "$revision"
    git -C "$part" checkout -q --detach FETCH_HEAD
    test "$(git -C "$part" rev-parse HEAD)" = "$revision" || fail "${name} is not at ${revision}."
    if test "$(pin "$name" submodules)" = true; then
      run "${name}: fetching submodules" git -C "$part" submodule update --init --recursive --depth=1
    fi
  else
    rm -rf "$dest"
    url="$(pin "$name" url)"
    sum="$(pin "$name" sha256)"
    archive="${DOWNLOADS}/${name}-${sum}.tar"
    if ! test -f "$archive" || test "$(sha256 "$archive")" != "$sum"; then
      mkdir -p "$DOWNLOADS"
      rm -f "${archive}.partial"
      run "${name}: downloading" curl --fail --location --retry 3 --connect-timeout 20 \
        --speed-limit 1024 --speed-time 60 --output "${archive}.partial" "$url"
      if test "$(sha256 "${archive}.partial")" != "$sum"; then
        rm -f "${archive}.partial"
        fail "${url} does not match its SHA-256 in config/sources.json."
      fi
      mv "${archive}.partial" "$archive"
    fi
    mkdir -p "$part"
    run "${name}: unpacking" tar -xf "$archive" -C "$part" --strip-components 1
  fi
  for patch in "$@"; do
    run "${name}: applying ${patch##*/}" git -C "$part" apply "$patch"
  done
  printf '%s\n' "$stamp" >"${part}/.tm2020-source"
  mv "$part" "$dest"
}

# Builds one library for x86_64 into DEPS.
build_library() {
  local name="$1" build="${WORK}/$1" configured
  shift
  configured="$(cat "${SOURCES}/${name}/.tm2020-source"; printf '%s\n' "$@")"
  if ! test -f "${build}/Makefile" || test "$(cat "${build}/.tm2020-configure" 2>/dev/null)" != "$configured"; then
    rm -rf "$build"
    mkdir -p "$build"
    run_in "$build" "${name}: configure" env "${LIBRARY_ENV[@]}" "${SOURCES}/${name}/configure" \
      --build=aarch64-apple-darwin --host=x86_64-apple-darwin --prefix="$DEPS" \
      --enable-shared --disable-static "$@"
    printf '%s\n' "$configured" >"${build}/.tm2020-configure"
  fi
  run_in "$build" "${name}: make" env "${LIBRARY_ENV[@]}" /usr/bin/make -j"$TM2020_JOBS"
  run_in "$build" "${name}: make install" env "${LIBRARY_ENV[@]}" /usr/bin/make -j"$TM2020_JOBS" install
}

build_moltenvk() {
  local source="${SOURCES}/moltenvk" lib
  local dylib="${source}/Package/Release/MoltenVK/dylib/macOS/libMoltenVK.dylib"
  if ! test -f "$dylib"; then
    rm -rf "${source}/External/build"
    run_in "$source" 'moltenvk: fetchDependencies' ./fetchDependencies --macos --keep-cache
    run_in "$source" 'moltenvk: make macos' /usr/bin/make macos
  fi
  cp "$dylib" "${DEPS}/lib/"
  # Wine finds these through @rpath once they are in the runtime.
  for lib in "${DEPS}/lib"/*.dylib; do
    test -L "$lib" || install_name_tool -id "@rpath/${lib##*/}" "$lib" 2>>"$LOG"
  done
}

build_wine() {
  local source="${SOURCES}/wine" build="${WORK}/wine" stage="${WORK}/wine-stage" configured
  local headers="${SOURCES}/moltenvk/External/Vulkan-Headers/include"
  # The soname values make Wine dlopen the libraries through @rpath, which finds them in the runtime.
  local options=(--prefix=/runtime --build=x86_64-apple-darwin --host=x86_64-apple-darwin
    --disable-tests '--enable-archs=i386,x86_64' --with-mingw
    --without-x --without-wayland --without-alsa --without-pulse --without-oss
    --without-gstreamer --without-sdl --without-usb --without-udev
    --without-cups --without-pcap --without-dbus
    "CC=clang -arch x86_64 ${PREFIX_MAP}" "CXX=clang++ -arch x86_64 ${PREFIX_MAP}"
    "CROSSCFLAGS=-O2 -g ${PREFIX_MAP}"
    "BISON=$(command -v bison)" "FLEX=$(command -v flex)"
    "CPPFLAGS=-I${headers}" "FREETYPE_CFLAGS=-I${DEPS}/include/freetype2"
    "FREETYPE_LIBS=-L${DEPS}/lib -lfreetype" "GNUTLS_CFLAGS=-I${DEPS}/include"
    "GNUTLS_LIBS=-L${DEPS}/lib -lgnutls"
    ac_cv_lib_soname_freetype=@rpath/libfreetype.6.dylib
    ac_cv_lib_soname_gnutls=@rpath/libgnutls.30.dylib
    ac_cv_lib_soname_MoltenVK=@rpath/libMoltenVK.dylib
    "LDFLAGS=-L${DEPS}/lib -Wl,-rpath,${DEPS}/lib -Wl,-headerpad_max_install_names")
  test -f "${headers}/vulkan/vulkan.h" || fail "MoltenVK did not provide the Vulkan headers in ${headers}."
  # Patches are left out on purpose: a changed patch only rebuilds the files it touches.
  configured="$(pin wine revision; printf '%s\n' "${options[@]}")"
  if ! test -f "${build}/config.status" || test "$(cat "${build}/.tm2020-configure" 2>/dev/null)" != "$configured"; then
    rm -rf "$build"
    mkdir -p "$build"
    run_in "$build" 'wine: configure' env "PKG_CONFIG_LIBDIR=${DEPS}/lib/pkgconfig" PKG_CONFIG_PATH= \
      /usr/bin/arch -x86_64 /bin/bash "${source}/configure" "${options[@]}"
    printf '%s\n' "$configured" >"${build}/.tm2020-configure"
  fi
  run_in "$build" 'wine: make' /usr/bin/arch -x86_64 /usr/bin/make -j"$TM2020_JOBS"
  rm -rf "$stage"
  run_in "$build" 'wine: make install-lib' /usr/bin/make -j"$TM2020_JOBS" install-lib DESTDIR="$stage"
  rm -rf "$TREE"
  ditto "${stage}/runtime" "$TREE"
}

# Prints the rpath that leads from a folder in the runtime to its lib folder.
rpath_to_lib() {
  local inside=0 rest up=''
  if test "$1" = "${TREE}/lib"; then
    echo '@loader_path/.'
    return
  fi
  case "$1" in
    "${TREE}/lib/"*) inside=1 rest="${1#"${TREE}/lib/"}" ;;
    *) rest="${1#"${TREE}/"}" ;;
  esac
  while test -n "$rest"; do
    up="${up}../"
    case "$rest" in
      */*) rest="${rest#*/}" ;;
      *) rest='' ;;
    esac
  done
  if test "$inside" = 1; then echo "@loader_path/${up%/}"; else echo "@loader_path/${up}lib"; fi
}

# Points a Mach-O file at the libraries in the runtime instead of the build folders.
relocate_macho() {
  local binary="$1" dep name rpath here
  while IFS= read -r dep; do
    name="${dep##*/}"
    case "$dep" in
      /System/* | /usr/lib/* | @loader_path/* | @executable_path/*) ;;
      *)
        if test -e "${TREE}/lib/${name}"; then
          test "$dep" = "@rpath/${name}" || install_name_tool -change "$dep" "@rpath/${name}" "$binary" 2>>"$LOG"
        elif ! [[ "$dep" == @rpath/* && -e "${binary%/*}/${name}" ]]; then
          fail "${binary#"${TREE}"/} needs ${dep}, which is not in the runtime."
        fi
        ;;
    esac
  done < <(otool -L "$binary" | /usr/bin/sed -n -E 's/^[[:space:]]+(.*) \(compatibility version.*/\1/p')
  while IFS= read -r rpath; do
    case "$rpath" in
      /*) install_name_tool -delete_rpath "$rpath" "$binary" 2>>"$LOG" ;;
    esac
  done < <(otool -l "$binary" | /usr/bin/awk '/cmd LC_RPATH/ {found = 1; next} found && $1 == "path" {print $2; found = 0}' | sort -u)
  here="$(rpath_to_lib "${binary%/*}")"
  if ! otool -l "$binary" | /usr/bin/awk -v want="$here" '$1 == "path" && $2 == want {found = 1} END {exit !found}'; then
    install_name_tool -add_rpath "$here" "$binary" 2>>"$LOG"
  fi
  strip -x "$binary" 2>>"$LOG"
  codesign --force --sign - "$binary" 2>>"$LOG"
}

write_licenses() {
  local name file licenses="${TREE}/licenses"
  mkdir -p "${licenses}/toolchain"
  cp "${PROJECT}/LICENSE" "${licenses}/Trackmania2020-MacOS.txt"
  cp "${PROJECT}/licenses/"* "${licenses}/toolchain/"
  for name in wine d9vk moltenvk "${LIBRARIES[@]}"; do
    while IFS= read -r -d '' file; do
      ditto "${SOURCES}/${name}/${file#./}" "${licenses}/${name}/${file#./}"
    done < <(cd "${SOURCES}/${name}" && find . -name .git -prune -o -type f \( -iname 'COPYING*' \
      -o -iname 'LICENSE*' -o -iname 'LICENCE*' -o -iname 'COPYRIGHT*' -o -iname 'NOTICE*' \
      -o -iname 'FTL.TXT' -o -iname 'README*' \) -print0)
  done
}

write_build_info() {
  {
    echo "Trackmania2020-MacOS runtime $(cat "${PROJECT}/VERSION")"
    echo "macOS $(sw_vers -productVersion)"
    xcodebuild -version | /usr/bin/sed -n 1p
    x86_64-w64-mingw32-gcc --version | /usr/bin/sed -n 1p
    echo
    echo 'Patches:'
    (cd "$PROJECT" && shasum -a 256 patches/wine/base/*.patch patches/wine/*.patch patches/d9vk/*.patch)
  } >"${TREE}/build-info.txt"
  cp "$PINS" "${TREE}/sources.json"
}

bundle() {
  local lib file started="$SECONDS"
  for lib in "${DEPS}/lib"/*.dylib; do
    rm -f "${TREE}/lib/${lib##*/}"
    if test -L "$lib"; then ln -s "$(readlink "$lib")" "${TREE}/lib/${lib##*/}"; else cp -p "$lib" "${TREE}/lib/"; fi
  done
  printf '  runtime: relinking, stripping and signing ... '
  while IFS= read -r -d '' file; do
    case "$(file -b "$file")" in
      *PE32+*) x86_64-w64-mingw32-strip --strip-debug "$file" ;;
      *PE32*) i686-w64-mingw32-strip --strip-debug "$file" ;;
      *Mach-O*) relocate_macho "$file" ;;
    esac
  done < <(find "$TREE" -type f -print0)
  echo "$((SECONDS - started)) s"
  write_licenses
  write_build_info
  run 'runtime: wine --version' "${TREE}/bin/wine" --version
}

build_dxvk() {
  local bits="$1" cross_file="$2" patch
  shift 2
  local source="${BUILD}/d9vk-${bits}-source" build="${BUILD}/d9vk-${bits}"
  rm -rf "$source" "$build"
  cp -R "${SOURCES}/d9vk" "$source"
  for patch in "$@"; do
    run "dxvk ${bits}: applying ${patch##*/}" patch --directory="$source" --strip=1 --fuzz=0 --input="$patch"
  done
  run "dxvk ${bits}: meson setup" meson setup --cross-file "${source}/${cross_file}" --buildtype=release \
    -Doptimization=3 "$build" "$source"
  run "dxvk ${bits}: ninja" ninja -C "$build" -j "$TM2020_JOBS" src/dxgi/dxgi.dll src/d3d11/d3d11.dll
}

# Fails if a file in the folder contains the checkout path, so nobody's home folder ships.
check_paths() {
  local status=0 found="${BUILD}/leaked-paths.txt"
  /usr/bin/grep -rlF "$PROJECT" "$1" >"$found" || status=$?
  case "$status" in
    0) sed -n 1,20p "$found" >&2 && fail "The checkout path is in the files above, in $1." ;;
    1) rm -f "$found" ;;
    *) fail "Could not search $1 for the checkout path." ;;
  esac
}

install_runtime() {
  local partial="${TM2020_RUNTIME}.partial" bits
  rm -rf "$partial"
  mkdir -p "$(dirname "$TM2020_RUNTIME")"
  ditto "$TREE" "$partial"
  for bits in x32 x64; do
    mkdir -p "${partial}/lib/dxvk/${bits}"
    cp "${BUILD}/d9vk-${bits}/src/dxgi/dxgi.dll" "${BUILD}/d9vk-${bits}/src/d3d11/d3d11.dll" "${partial}/lib/dxvk/${bits}/"
  done
  mkdir -p "${partial}/libexec"
  x86_64-w64-mingw32-gcc -O2 -mwindows -o "${partial}/libexec/connect-idle.exe" "${PROJECT}/tools/connect-idle.c"
  check_paths "$partial"
  mv "$partial" "$TM2020_RUNTIME"
}

if test -e "$TM2020_RUNTIME"; then
  fail "${TM2020_RUNTIME} already exists. Remove it first, or set TM2020_RUNTIME to another folder."
fi

check_host
mkdir -p "$BUILD"
: >"$LOG"
echo "Building with ${TM2020_JOBS} jobs. The full log is ${LOG}"
echo '== Sources'
fetch wine "${PROJECT}/patches/wine/base/"*.patch "${PROJECT}/patches/wine/"*.patch
fetch d9vk
fetch moltenvk
for name in "${LIBRARIES[@]}"; do fetch "$name"; done
echo '== Libraries'
rm -rf "$DEPS"
mkdir -p "${DEPS}/lib"
build_library freetype --without-harfbuzz --without-brotli --without-png --without-bzip2
build_library gmp --disable-cxx
build_library nettle --disable-documentation --disable-openssl
build_library libtasn1 --disable-doc --disable-tools
build_library libunistring --disable-rpath
# A neutral sysconfdir keeps the build path out of the library.
build_library gnutls --disable-doc --disable-tests --disable-tools --disable-cxx \
  --without-p11-kit --without-idn --without-brotli --without-zstd \
  --with-included-unistring=no --disable-nls --sysconfdir=/etc
build_moltenvk
echo '== Wine'
build_wine
bundle
echo '== DXVK'
# Connect passed Ubisoft's sign-in with only the first patch, so it stays that way.
build_dxvk x32 build-win32.txt "${PROJECT}/patches/d9vk/0001-"*.patch
build_dxvk x64 build-win64.txt "${PROJECT}/patches/d9vk/"*.patch
install_runtime
echo "Runtime installed in ${TM2020_RUNTIME} after $((SECONDS / 60)) minutes"
