#!/bin/sh
# Builds a best-effort AppImage from an installed tree.
#
# The AppImage bundles Qt 6, KF6 and the QML modules, so it is larger than the
# native packages and is only a convenience artifact. Run from the repository
# root:
#   packaging/appimage/build-appimage.sh [build-dir]
#
# Requires curl. linuxdeploy and appimagetool are downloaded on first use.
set -eu

build_dir="${1:-build}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

DESTDIR="$work/AppDir" cmake --install "$build_dir" --prefix /usr

# The desktop file was generated with the build tree's install prefix (often
# /usr/local), while the AppDir always uses /usr. The Exec entries have to use
# the binary name so appimagetool can resolve it inside the AppDir.
desktop="$work/AppDir/usr/share/applications/org.nekoshot.NekoShot.desktop"
sed -i 's|^Exec=[^ ]*/nekoshot|Exec=nekoshot|' "$desktop"

# NekoShot's own QML is compiled into the binary, so the Qt QML import scanner
# cannot see Kirigami, QtQuick Controls or KQuickImageEditor. Ship the whole
# system QML module tree instead; linuxdeploy then resolves its libraries.
mkdir -p "$work/AppDir/usr/qml"
cp -a /usr/lib/qt6/qml/. "$work/AppDir/usr/qml/"

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
download "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" "$tools/appimagetool"
# appimagetool cannot follow the 302 that GitHub returns for the runtime, so
# fetch it explicitly and hand it over.
[ -f "$tools/runtime-x86_64" ] || curl -L --fail -o "$tools/runtime-x86_64" \
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"

export QMAKE="$(command -v qmake6 || command -v qmake)"
# Works without FUSE and avoids strip failures on some distributions.
export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

"$tools/linuxdeploy" --appdir "$work/AppDir" \
    --desktop-file "$desktop" \
    --icon-file "$work/AppDir/usr/share/icons/hicolor/scalable/apps/nekoshot.svg" \
    --plugin qt

# linuxdeploy makes AppRun a symlink to usr/bin/nekoshot. That breaks the
# $ORIGIN-relative RUNPATH when the image is extracted to a different directory,
# so use a wrapper that sets the search paths explicitly.
rm -f "$work/AppDir/AppRun"
cat > "$work/AppDir/AppRun" <<'WRAPPER'
#!/bin/sh
HERE=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML_IMPORT_PATH="$HERE/usr/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}"
exec "$HERE/usr/bin/nekoshot" "$@"
WRAPPER
chmod +x "$work/AppDir/AppRun"

version=$(sed -n 's/^set(PROJECT_VERSION "\(.*\)")/\1/p' CMakeLists.txt)
OUTPUT="$PWD/nekoshot-$version-x86_64.AppImage"
export ARCH=x86_64
"$tools/appimagetool" --runtime-file "$tools/runtime-x86_64" "$work/AppDir" "$OUTPUT"
echo "wrote $OUTPUT"
