#!/bin/sh
# Builds a best-effort AppImage from an installed tree.
#
# The AppImage bundles Qt 6 and KF6, so it is larger than the native packages
# and is only a convenience artifact. Run from the repository root:
#   packaging/appimage/build-appimage.sh [build-dir]
#
# Requires linuxdeploy and linuxdeploy-plugin-qt (downloaded on first use).
set -eu

build_dir="${1:-build}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

DESTDIR="$work/AppDir" cmake --install "$build_dir" --prefix /usr

download() {
    url="$1"
    out="$2"
    [ -x "$out" ] || curl -L --fail -o "$out" "$url"
    chmod +x "$out"
}

tools="$work/tools"
mkdir -p "$tools"
download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$tools/linuxdeploy"
download "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$tools/linuxdeploy-plugin-qt"

export QMAKE="$(command -v qmake6 || command -v qmake)"
export EXTRA_QT_MODULES="multimedia"
export OUTPUT="$PWD/nekoshot-$(sed -n 's/^set(PROJECT_VERSION "\(.*\)")/\1/p' CMakeLists.txt)-x86_64.AppImage"
"$tools/linuxdeploy" --appdir "$work/AppDir" \
    --desktop-file "$work/AppDir/usr/share/applications/org.nekoshot.NekoShot.desktop" \
    --icon-file "$work/AppDir/usr/share/icons/hicolor/scalable/apps/nekoshot.svg" \
    --plugin qt --output appimage

echo "wrote $OUTPUT"
