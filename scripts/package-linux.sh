#!/usr/bin/env bash
# Packs the Linux Release build into what a release publishes, in out/package (or OUT_DIR):
#   AnimeHub-<version>-x86_64.AppImage   the app with Qt and every library a desktop may lack
#                                        (built on Ubuntu 24.04: glibc 2.39 or newer);
#   animehub_<version>_amd64.deb         for Ubuntu 24.04 and later: Qt in /opt/animehub, the
#                                        rest from the system's packages (mpv and aria2 too);
#   SHA256SUMS.txt                       checksums of both.
# Run on Ubuntu 24.04 after `cmake --preset linux-release && cmake --build --preset
# linux-release`, with QT_ROOT set to Qt's linux_gcc_64 kit; needs patchelf, dpkg-dev and, for
# --smoke, xvfb and x11-utils. linuxdeploy and its Qt plugin are pinned by SHA-256 and cached in
# out/engine-cache. Without FUSE (containers, CI) set APPIMAGE_EXTRACT_AND_RUN=1.
#   --smoke   opens the AppImage's window under Xvfb and captures the .deb's offscreen, each with
#             a throwaway library and an API that answers nothing.
set -euo pipefail

repository="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$repository/out/build/linux-release}"
cache="${ANIMEHUB_ENGINE_CACHE:-$repository/out/engine-cache}"
out="${OUT_DIR:-$repository/out/package}"
# What the packages are made from; only the packages and their checksums stay.
stage="$out/stage"
appdir="$stage/AppDir"
app_id="io.github.duardor968.AnimeHub"
smoke=false
[[ "${1:-}" == "--smoke" ]] && smoke=true

version="$(sed -nE 's/^[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)$/\1/p' \
  "$repository/CMakeLists.txt" | head -n 1)"
if [[ ! -x "$build/animehub" ]]; then
  echo "There is no Release build in $build: build the linux-release preset first." >&2
  exit 1
fi
if [[ ! -x "${QT_ROOT:-}/bin/qmake" ]]; then
  echo "Set QT_ROOT to Qt 6.11's linux_gcc_64 kit." >&2
  exit 1
fi

# fetch <folder> <file> <url> <sha256>: prints the path of the verified, executable tool.
fetch() {
  local path="$cache/$1/$2"
  if [[ -f "$path" ]] && ! echo "$4  $path" | sha256sum --check --status; then
    rm -f "$path"
  fi
  if [[ ! -f "$path" ]]; then
    mkdir -p "$cache/$1"
    curl -fsSL -o "$path.partial" "$3"
    if ! echo "$4  $path.partial" | sha256sum --check --status; then
      rm -f "$path.partial"
      echo "$2 does not match its pinned SHA-256" >&2
      exit 1
    fi
    mv "$path.partial" "$path"
  fi
  chmod +x "$path"
  printf '%s' "$path"
}
linuxdeploy="$(fetch linuxdeploy-1-alpha-20251107-1 linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
  c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d)"
qt_plugin="$(fetch linuxdeploy-plugin-qt-1-alpha-20250213-1 linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
  15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724)"

rm -rf "$out"
mkdir -p "$appdir/usr/bin"
install -m 755 "$build/animehub" "$appdir/usr/bin/animehub"
# Next to the executable, where Ajustes > Acerca de looks for them.
install -m 644 "$repository/LICENSE" "$appdir/usr/bin/LICENSE.txt"
install -m 644 "$repository/packaging/THIRD-PARTY.txt" "$appdir/usr/bin/THIRD-PARTY.txt"
install -D -m 644 "$repository/packaging/licenses/LGPL-3.0.txt" \
  "$appdir/usr/bin/licenses/Qt/LGPL-3.0.txt"
install -D -m 644 "$build/icon-font/LICENSE.md" "$appdir/usr/bin/licenses/Uno.Fonts/LICENSE.md"
icon="$stage/$app_id.png"
install -m 644 "$repository/assets/animehub.png" "$icon"

# linuxdeploy's Qt plugin takes every plugin of a kind it deploys. It sees a plugins folder
# without what AnimeHub never loads, as on Windows: QML debugging, other databases and image
# formats (Qt's TIFF one needs RHEL's libtiff.so.5), the certificate-only TLS backend.
plugins="$stage/qt-plugins"
cp -as "$QT_ROOT/plugins" "$plugins"
rm -rf "$plugins/qmltooling" "$plugins/tls/libqcertonlybackend.so" \
  "$plugins/sqldrivers/libqsqlmimer.so" "$plugins/sqldrivers/libqsqlodbc.so" \
  "$plugins/sqldrivers/libqsqlpsql.so" "$plugins/sqldrivers/libqsqlmysql.so" \
  "$plugins/sqldrivers/libqsqlibase.so" "$plugins/sqldrivers/libqsqloci.so" \
  "$plugins/imageformats/libqicns.so" "$plugins/imageformats/libqtga.so" \
  "$plugins/imageformats/libqtiff.so" "$plugins/imageformats/libqwbmp.so"
cat > "$stage/qmake" <<EOF
#!/usr/bin/env bash
"$QT_ROOT/bin/qmake" "\$@" |
  sed -E 's|^(QT_INSTALL_PLUGINS(/[a-z]+)?:).*|\1$plugins|; s|^$QT_ROOT/plugins\$|$plugins|'
EOF
chmod +x "$stage/qmake"

# Qt, those plugins and the QML modules, and the libraries they load. OpenSSL stays the
# system's: Qt loads libssl at run time and it must match the libcrypto in use. The offscreen
# platform is for captures without a display.
export QMAKE="$stage/qmake"
export QML_SOURCES_PATHS="$repository/qml"
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export LD_LIBRARY_PATH="$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$(dirname "$qt_plugin"):$PATH"
"$linuxdeploy" --appdir "$appdir" \
  --executable "$appdir/usr/bin/animehub" \
  --desktop-file "$repository/packaging/linux/$app_id.desktop" \
  --icon-file "$icon" \
  --exclude-library 'libcrypto.so*' --exclude-library 'libssl.so*' \
  --plugin qt

# The Controls styles other than FluentWinUI3 and the Fusion and Basic it builds on, as on
# Windows, and the libcrypto the Qt plugin copies regardless of the exclusion.
usr="$appdir/usr"
rm -rf "$usr/qml/QtQuick/Controls/Imagine" "$usr/qml/QtQuick/Controls/Material" \
  "$usr/qml/QtQuick/Controls/Universal" "$usr/qml/QtQuick/NativeStyle" \
  "$usr"/lib/libQt6QuickControls2{Imagine,Material,Universal}{,StyleImpl}.so* \
  "$usr"/lib/libcrypto.so* "$usr"/lib/libssl.so*

# The Ubuntu package each library came from, or nothing for Qt's own.
package_of() {
  dpkg -S "$(basename "$1")" 2>/dev/null | head -n 1 | cut -d: -f1 || true
}

# The .deb: the same Qt in /opt/animehub; what Ubuntu packages, from Ubuntu.
root="$stage/deb/animehub_${version}_amd64"
mkdir -p "$root/opt/animehub" "$root/usr/bin" "$root/usr/share/applications" \
  "$root/usr/share/icons/hicolor/256x256/apps" "$root/usr/share/doc/animehub" "$root/DEBIAN"
cp -a "$usr/bin" "$usr/lib" "$usr/plugins" "$usr/qml" "$root/opt/animehub/"
for library in "$root"/opt/animehub/lib/*.so*; do
  if [[ -n "$(package_of "$library")" ]]; then
    rm -f "$library"
  fi
done
ln -s /opt/animehub/bin/animehub "$root/usr/bin/animehub"
install -m 644 "$repository/packaging/linux/$app_id.desktop" "$root/usr/share/applications/"
install -m 644 "$icon" "$root/usr/share/icons/hicolor/256x256/apps/"
{
  echo "AnimeHub Desktop: https://github.com/duardor968/animehub-desktop"
  echo "Licencia: GPL-3.0-or-later (/opt/animehub/bin/LICENSE.txt)."
  echo
  cat "$repository/packaging/THIRD-PARTY.txt"
} > "$root/usr/share/doc/animehub/copyright"
# dpkg-shlibdeps reads a source package's control file; the libraries of Qt carry no package.
mkdir -p "$stage/deb/work/debian"
printf 'Source: animehub\n\nPackage: animehub\nArchitecture: amd64\n' \
  > "$stage/deb/work/debian/control"
mapfile -t elves < <(find "$root/opt/animehub" -type f -print0 |
  xargs -0 file --no-pad | grep -E ': ELF ' | cut -d: -f1)
shlibs="$(cd "$stage/deb/work" && dpkg-shlibdeps -O --ignore-missing-info \
  -l"$root/opt/animehub/lib" "${elves[@]}" | sed -n 's/^shlibs:Depends=//p')"
if [[ -z "$shlibs" ]]; then
  echo "dpkg-shlibdeps found no dependencies." >&2
  exit 1
fi
# Beyond what it links: the libssl Qt loads for HTTPS, the certificates that check the API's,
# and the engines.
cat > "$root/DEBIAN/control" <<EOF
Package: animehub
Version: $version
Architecture: amd64
Maintainer: duardor968 <duardor968@users.noreply.github.com>
Installed-Size: $(du -sk --exclude=DEBIAN "$root" | cut -f1)
Depends: $shlibs, libssl3, ca-certificates, mpv, aria2
Section: video
Priority: optional
Homepage: https://github.com/duardor968/animehub-desktop
Description: Biblioteca de anime con catálogo y descargas
 AnimeHub Desktop guarda tu biblioteca de anime, consulta el catálogo de AnimeHub,
 descarga episodios de MEGA y Pixeldrain con aria2 y lleva la cuenta de lo visto
 con mpv.
EOF
deb="$out/animehub_${version}_amd64.deb"
dpkg-deb --build --root-owner-group "$root" "$deb" >/dev/null

# The AppImage carries the copyright notice of every Ubuntu library in it.
for library in "$usr"/lib/*.so*; do
  package="$(package_of "$library")"
  if [[ -n "$package" && -f "/usr/share/doc/$package/copyright" ]]; then
    install -D -m 644 "/usr/share/doc/$package/copyright" "$usr/share/doc/$package/copyright"
  fi
done
appimage="$out/AnimeHub-$version-x86_64.AppImage"
LDAI_OUTPUT="$appimage" "$linuxdeploy" --appdir "$appdir" \
  --exclude-library 'libcrypto.so*' --exclude-library 'libssl.so*' --output appimage >/dev/null

(cd "$out" && sha256sum "$(basename "$appimage")" "$(basename "$deb")" > SHA256SUMS.txt)

if $smoke; then
  data="$(mktemp -d)"
  export ANIMEHUB_API_URL="http://127.0.0.1:9/api/v1/"
  # The .deb's tree against the system's libraries, without Qt's kit on the library path.
  if ! env -u LD_LIBRARY_PATH QT_QPA_PLATFORM=offscreen "$root/opt/animehub/bin/animehub" \
    --data-dir "$data/deb" --smoke-screenshot "$data/deb.png" 2>"$data/deb.log"; then
    cat "$data/deb.log" "$data/deb.png.log" >&2 || true
    echo "The .deb's AnimeHub did not render its window." >&2
    exit 1
  fi
  echo "The .deb's AnimeHub rendered its window offscreen."

  Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$data/display" 2>/dev/null &
  xvfb=$!
  for _ in $(seq 1 50); do
    [[ -s "$data/display" ]] && break
    sleep 0.1
  done
  display=":$(cat "$data/display")"
  # Its own session, to end everything it starts.
  DISPLAY="$display" env -u LD_LIBRARY_PATH setsid "$appimage" --data-dir "$data/appimage" \
    2>"$data/appimage.log" &
  app=$!
  opened=false
  for _ in $(seq 1 150); do
    if DISPLAY="$display" xwininfo -root -tree 2>/dev/null | grep -q '"AnimeHub"'; then
      opened=true
      break
    fi
    kill -0 "$app" 2>/dev/null || break
    sleep 0.2
  done
  # Still running a moment later.
  if $opened; then
    sleep 3
    kill -0 "$app" 2>/dev/null || opened=false
  fi
  kill -TERM -- "-$app" 2>/dev/null || true
  wait "$app" 2>/dev/null || true
  kill "$xvfb" 2>/dev/null || true
  wait "$xvfb" 2>/dev/null || true
  if ! $opened; then
    tail -n 30 "$data/appimage.log" >&2
    rm -rf "$data"
    echo "The AppImage did not open its window, or closed right after." >&2
    exit 1
  fi
  rm -rf "$data"
  echo "The AppImage opened its window."
fi

rm -rf "$stage"
for file in "$appimage" "$deb"; do
  printf '%s  %s\n' "$(basename "$file")" "$(du -h "$file" | cut -f1)"
done
